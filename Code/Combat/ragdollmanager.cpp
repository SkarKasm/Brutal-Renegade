// Copyright (C) 2026 Brutal Renegade contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../ActiveRagdoll/active_ragdoll.h"
#include "../ActiveRagdoll/human_skeleton.h"
#include "ragdollmanager.h"
#include "soldier.h"
#include "combat.h"
#include "gametype.h"
#include "humanphys.h"
#include "damage.h"
#include "goremanager.h"
#include "gameobjmanager.h"
#include "pscene.h"
#include "phys.h"
#include "mesh.h"
#include "meshmdl.h"
#include "hlod.h"
#include "htree.h"
#include "rendobj.h"
#include <cstdio>
#include <cstring>

namespace {
using BRRagdoll::World;
using BRRagdoll::HitRegion;
World world;
bool enabled = false;
FILE *logFile = NULL;

using BRRagdoll::Segment;
constexpr auto &segments = BRRagdoll::HumanSegments;
constexpr int PartCount = BRRagdoll::HumanPartCount;

struct Entry {
    int id = 0;
    RenderObjClass *model = NULL;
    World::Handle handle = 0;
    std::array<int, PartCount> bones{};
    std::array<Matrix3D, PartCount> offset;
};
std::array<Entry, World::MaxBodies> entries;

void Log(const char *text) {
    if (!logFile) return;
    fputs(text, logFile);
    fputc('\n', logFile);
    fflush(logFile);
}

void ReleasePose(Entry &entry) {
    for (int bone : entry.bones) {
        if (bone > 0) entry.model->Release_Bone(bone);
    }
}

void RemoveEntry(Entry &entry) {
    if (!entry.handle) return;
    ReleasePose(entry);
    world.Remove(entry.handle);
    entry.model->Release_Ref();
    entry = Entry{};
}

Matrix3D Transform(const float *position, const float *axes) {
    Matrix3D result(1);
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) result[row][column] = axes[column * 3 + row];
    }
    result.Set_Translation(Vector3(position[0], position[1], position[2]));
    return result;
}

void Axes(const Matrix3D &transform, float *axes) {
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) axes[column * 3 + row] = transform[row][column];
    }
}

void AddTerrain(RenderObjClass *object) {
    if (!object) return;
    if (object->Class_ID() == RenderObjClass::CLASSID_MESH &&
        (object->Get_Collision_Type() & COLLISION_TYPE_PHYSICAL)) {
        MeshModelClass *mesh = static_cast<MeshClass *>(object)->Peek_Model();
        const Vector3 *vertices = mesh->Get_Vertex_Array();
        const TriIndex *triangles = mesh->Get_Polygon_Array();
        const Matrix3D &transform = object->Get_Transform();
        for (int i = 0; i < mesh->Get_Polygon_Count(); ++i) {
            Vector3 a, b, c;
            Matrix3D::Transform_Vector(transform, vertices[triangles[i][0]], &a);
            Matrix3D::Transform_Vector(transform, vertices[triangles[i][1]], &b);
            Matrix3D::Transform_Vector(transform, vertices[triangles[i][2]], &c);
            if (Vector3::Cross_Product(b-a, c-a).Length2() > .00000001f)
                Phys_AddWorldTriangle(&a.X, &b.X, &c.X);
        }
    }
    if (object->Class_ID() == RenderObjClass::CLASSID_HLOD) {
        HLodClass *lod = static_cast<HLodClass *>(object);
        const int detail = lod->Get_Lod_Count() - 1;
        if (detail >= 0) {
            for (int i = 0; i < lod->Get_Lod_Model_Count(detail); ++i)
                AddTerrain(lod->Peek_Lod_Model(detail, i));
        }
        for (int i = 0; i < lod->Get_Additional_Model_Count(); ++i)
            AddTerrain(lod->Peek_Additional_Model(i));
        return;
    }
    for (int i = 0; i < object->Get_Num_Sub_Objects(); ++i) {
        RenderObjClass *child = object->Get_Sub_Object(i);
        AddTerrain(child);
        if (child) child->Release_Ref();
    }
}

Entry *Find(int id) {
    for (Entry &entry : entries) if (entry.handle && entry.id == id) return &entry;
    return NULL;
}

Entry *Create(SoldierGameObj *soldier) {
    Entry *entry = NULL;
    for (Entry &candidate : entries) {
        if (!candidate.handle) { entry = &candidate; break; }
    }
    if (!entry) return NULL;
    RenderObjClass *model = soldier->Peek_Model();
    if (!model || !model->Get_HTree()) return NULL;
    std::array<physPart_t, PartCount> parts{};
    Vector3 inheritedVelocity;
    soldier->Peek_Human_Phys()->Get_Velocity(&inheritedVelocity);
    for (int i = 0; i < PartCount; ++i) {
        const Segment &segment = segments[i];
        const int bone = model->Get_Bone_Index(segment.bone);
        const int tip = segment.tip ? model->Get_Bone_Index(segment.tip) : bone;
        if (bone <= 0 || tip <= 0) {
            Log("Ragdoll skipped: missing skeleton segment.");
            *entry = Entry{};
            return NULL;
        }
        entry->bones[i] = bone;
        const Vector3 a = model->Get_Bone_Transform(bone).Get_Translation();
        const Vector3 b = segment.tip ? model->Get_Bone_Transform(tip).Get_Translation() :
                         a + Vector3(0, 0, .18f);
        physPart_t &part = parts[i];
        part.parent = segment.parent;
        part.shape = PHYS_SHAPE_CAPSULE;
        for (int k = 0; k < 3; ++k) {
            part.a[k] = a[k]; part.b[k] = b[k]; part.pivot[k] = a[k];
            part.vel[k] = inheritedVelocity[k];
        }
        part.radius = segment.radius;
        part.mass = segment.mass;
        part.joint = segment.parent < 0 ? PHYS_JOINT_NONE : PHYS_JOINT_SWINGTWIST;
        Vector3 direction = b - a;
        if (direction.Length2() < .0001f) { *entry = Entry{}; return NULL; }
        direction.Normalize();
        for (int k = 0; k < 3; ++k) part.twist1[k] = part.twist2[k] = direction[k];
        part.plane[0] = 1;
        part.planeHalf = part.normalHalf = 55;
        part.twistMin = -35;
        part.twistMax = 35;
    }
    entry->handle = world.Create(parts.data(), PartCount);
    if (!entry->handle) { *entry = Entry{}; return NULL; }
    entry->id = soldier->Get_ID();
    entry->model = model;
    model->Add_Ref();
    std::array<float, PHYS_MAX_PARTS * 3> positions{};
    std::array<float, PHYS_MAX_PARTS * 9> axes{};
    world.Pose(entry->handle, positions.data(), axes.data());
    for (int i = 0; i < PartCount; ++i) {
        Matrix3D inverse;
        Transform(positions.data()+i*3, axes.data()+i*9).Get_Orthogonal_Inverse(inverse);
        Matrix3D::Multiply(inverse, model->Get_Bone_Transform(entry->bones[i]), &entry->offset[i]);
    }
    Log("Created 11-part Jolt corpse from the current animated pose.");
    return entry;
}

int HitPart(const char *box) {
    if (!box) return 1;
    if (strstr(box,"HEAD") || strstr(box,"NECK")) return 2;
    if (strstr(box,"L UPPERARM")) return 3;
    if (strstr(box,"L FOREARM") || strstr(box,"L HAND")) return 4;
    if (strstr(box,"R UPPERARM")) return 5;
    if (strstr(box,"R FOREARM") || strstr(box,"R HAND")) return 6;
    if (strstr(box,"L THIGH")) return 7;
    if (strstr(box,"L CALF") || strstr(box,"L FOOT")) return 8;
    if (strstr(box,"R THIGH")) return 9;
    if (strstr(box,"R CALF") || strstr(box,"R FOOT")) return 10;
    return 1;
}
}

RagdollManager::BulletImpact *RagdollManager::CurrentImpact = NULL;

RagdollManager::BulletImpact::BulletImpact(SoldierGameObj *victim, const OffenseObjectClass &damage,
    const Vector3 &velocity, const Vector3 &contact)
    : previous(CurrentImpact), soldier(victim), offense(&damage), consumed(false) {
    for (int axis = 0; axis < 3; ++axis) { point[axis] = contact[axis]; direction[axis] = velocity[axis]; }
    CurrentImpact = this;
}

RagdollManager::BulletImpact::~BulletImpact() {
    CurrentImpact = previous;
}

void RagdollManager::Load_Level() {
    Unload_Level();
    if (!IS_SOLOPLAY || !COMBAT_SCENE) return;
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char *slash = strrchr(path, '\\');
    if (!slash || slash - path + 1 + 12 >= MAX_PATH) return;
    strcpy(slash + 1, "ragdoll.ini");
    if (!GetPrivateProfileIntA("Ragdolls", "Enabled", 0, path)) return;
    strcpy(slash + 1, "ragdoll.log");
    logFile = fopen(path, "w");
    enabled = world.Init();
    if (!enabled) { Log("Jolt initialization failed."); return; }
    Phys_BeginWorld();
    RefPhysListIterator iterator = COMBAT_SCENE->Get_Static_Object_Iterator();
    for (iterator.First(); !iterator.Is_Done(); iterator.Next()) AddTerrain(iterator.Peek_Obj()->Peek_Model());
    Phys_EndWorld();
    int bodies, active, hulls, triangles;
    Phys_Stats(&bodies, &active, &hulls, &triangles);
    if (!triangles) {
        Log("No physical terrain triangles were exported; retaining native corpse animation.");
        world.Shutdown();
        enabled = false;
        return;
    }
    if (logFile) { fprintf(logFile, "Imported %d static collision triangles.\n", triangles); fflush(logFile); }
    Log("Shared flatscreen/VR Jolt world initialized with static level geometry.");
}

void RagdollManager::Unload_Level() {
    for (Entry &entry : entries) RemoveEntry(entry);
    world.Shutdown();
    enabled = false;
    if (logFile) fclose(logFile);
    logFile = NULL;
}

void RagdollManager::Remove(SoldierGameObj *soldier) {
    if (!soldier) return;
    Entry *entry = Find(soldier->Get_ID());
    if (entry) RemoveEntry(*entry);
}

void RagdollManager::Damaged(SoldierGameObj *soldier, const OffenseObjectClass &damage,
    const Vector3 &direction, const char *box, float before, float after) {
    if (!enabled || !IS_SOLOPLAY || !soldier || soldier == COMBAT_STAR ||
        after > 0 || damage.Get_Damage() <= 0) return;
    const ArmorWarheadManager::SpecialDamageType type = ArmorWarheadManager::Get_Special_Damage_Type(damage.Get_Warhead());
    if (type == ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CHEM ||
        type == ArmorWarheadManager::SPECIAL_DAMAGE_TYPE_CNC_CHEM || GoreManager::Keep_Chemical_Corpse(soldier)) {
        Remove(soldier);
        return;
    }
    Entry *entry = Find(soldier->Get_ID());
    if (!entry) entry = Create(soldier);
    if (!entry) return;
    const int part = HitPart(box);
    if (before > 0) {
        HitRegion region = HitRegion::Chest;
        if (damage.ExplosionDamage) region = HitRegion::Blast;
        else if (part == 2) region = HitRegion::Head;
        else if (part >= 7) region = HitRegion::Leg;
        else if (part >= 3) region = HitRegion::Arm;
        else if (box && (strstr(box,"ABDOMEN") || strstr(box,"PELVIS"))) region = HitRegion::Abdomen;
        world.BeginDeath(entry->handle, region, part);
    }
    BulletImpact *impact = CurrentImpact;
    if (impact && !impact->consumed && impact->soldier == soldier && impact->offense == &damage) {
        impact->consumed = true;
        world.BulletHit(entry->handle, part, impact->direction, impact->point, damage.Get_Damage());
        if (before > 0) Log("Applied killing projectile impulse at its surface contact.");
    } else {
        Vector3 velocity = direction;
        if (velocity.Length2() > .000001f) velocity.Normalize();
        const Vector3 point = entry->model->Get_Bone_Transform(entry->bones[part]).Get_Translation();
        if (damage.ExplosionDamage) {
            velocity *= WWMath::Clamp(damage.Get_Damage() * .025f, .2f, 4.f);
            world.Hit(entry->handle, part, &velocity.X, &point.X);
        } else {
            world.BulletHit(entry->handle, part, &velocity.X, &point.X, damage.Get_Damage());
        }
    }
}

void RagdollManager::Update(float seconds) {
    if (!enabled || !IS_SOLOPLAY || CombatManager::Is_Game_Paused()) return;
    // Sample the game's animation before replacing bone transforms for presentation.
    for (Entry &entry : entries) {
        if (!entry.handle) continue;
        PhysicalGameObj *object = GameObjManager::Find_PhysicalGameObj(entry.id);
        if (!object || object->Peek_Model() != entry.model || object->Get_Defense_Object()->Get_Health() > 0) {
            RemoveEntry(entry);
            continue;
        }
        ReleasePose(entry);
        std::array<float, PHYS_MAX_PARTS * 9> targets{};
        for (int i = 0; i < PartCount; ++i) {
            Matrix3D inverse, target;
            entry.offset[i].Get_Orthogonal_Inverse(inverse);
            Matrix3D::Multiply(entry.model->Get_Bone_Transform(entry.bones[i]), inverse, &target);
            Axes(target, targets.data()+i*9);
            if (entry.model->Is_Bone_Skin_Hidden(entry.bones[i])) world.DisablePart(entry.handle, i);
        }
        world.SetTarget(entry.handle, targets.data());
    }
    world.Advance(seconds);
    for (Entry &entry : entries) {
        if (!entry.handle) continue;
        std::array<float, PHYS_MAX_PARTS * 3> positions{};
        std::array<float, PHYS_MAX_PARTS * 9> axes{};
        world.Pose(entry.handle, positions.data(), axes.data());
        Matrix3D inverse;
        entry.model->Get_Transform().Get_Orthogonal_Inverse(inverse);
        for (int i = 0; i < PartCount; ++i) {
            Matrix3D bone, relative;
            Matrix3D::Multiply(Transform(positions.data()+i*3, axes.data()+i*9), entry.offset[i], &bone);
            Matrix3D::Multiply(inverse, bone, &relative);
            entry.model->Capture_Bone(entry.bones[i]);
            entry.model->Control_Bone_Root_Relative(entry.bones[i], relative);
        }
    }
}
