// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only W3D/Jolt integration check. No window, game startup, renderer or headset.
#include "active_ragdoll.h"
#include "human_skeleton.h"
#include "always.h"
#include "ramfile.h"
#include "chunkio.h"
#include "htree.h"
#include <fstream>
#include <vector>
#include <cstdio>
#include <cmath>

static Matrix3D Transform(const float *p, const float *axis) {
    Matrix3D result(1);
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row) result[row][column] = axis[column*3+row];
    result.Set_Translation(Vector3(p[0],p[1],p[2]));
    return result;
}

int main(int argc, char **argv) {
    using namespace BRRagdoll;
    if (argc != 2) { std::puts("usage: ragdoll_engine_check hierarchy.w3d"); return 1; }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 1;
    std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
    RAMFileClass file(data.data(), static_cast<int>(data.size()));
    if (!file.Open(FileClass::READ)) return 1;
    ChunkLoadClass chunks(&file);
    if (!chunks.Open_Chunk() || chunks.Cur_Chunk_ID() != 0x100) return 1;
    HTreeClass tree;
    if (tree.Load_W3D(chunks) != HTreeClass::OK) return 1;
    chunks.Close_Chunk();
    Matrix3D root(1);
    root.Rotate_Z(.37f);
    root.Set_Translation(Vector3(3,-2,2));
    tree.Base_Update(root);
    std::array<int, HumanPartCount> bones{};
    std::array<Matrix3D, HumanPartCount> original, offsets;
    std::array<physPart_t, HumanPartCount> parts{};
    for (int i = 0; i < HumanPartCount; ++i) {
        const Segment &segment = HumanSegments[i];
        bones[i] = tree.Get_Bone_Index(segment.bone);
        int tip = segment.tip ? tree.Get_Bone_Index(segment.tip) : bones[i];
        if (bones[i] <= 0 || tip <= 0) {
            std::printf("FAIL missing bone %s / %s\n", segment.bone, segment.tip ? segment.tip : "head tip");
            return 1;
        }
        original[i] = tree.Get_Transform(bones[i]);
        Vector3 a = original[i].Get_Translation();
        Vector3 b = segment.tip ? tree.Get_Transform(tip).Get_Translation() : a+Vector3(0,0,.18f);
        Vector3 direction = b-a;
        const float length = direction.Length();
        std::printf("%s length %.4f m\n", segment.bone, length);
        if (length < .01f || length > .8f) return 1;
        direction.Normalize();
        physPart_t &part = parts[i];
        part.parent = segment.parent;
        part.shape = PHYS_SHAPE_CAPSULE;
        part.radius = segment.radius;
        part.mass = segment.mass;
        part.joint = i ? PHYS_JOINT_SWINGTWIST : PHYS_JOINT_NONE;
        for (int k = 0; k < 3; ++k) {
            part.a[k]=a[k]; part.b[k]=b[k]; part.pivot[k]=a[k];
            part.twist1[k]=part.twist2[k]=direction[k];
        }
        part.plane[0]=1;
        part.planeHalf=part.normalHalf=55;
        part.twistMin=-35;
        part.twistMax=35;
    }
    World world;
    if (!world.Init()) return 1;
    auto body = world.Create(parts.data(), HumanPartCount);
    if (!body) return 1;
    float positions[PHYS_MAX_PARTS*3], axes[PHYS_MAX_PARTS*9];
    world.Pose(body, positions, axes);
    for (int i = 0; i < HumanPartCount; ++i) {
        Matrix3D inverse;
        Transform(positions+i*3, axes+i*9).Get_Orthogonal_Inverse(inverse);
        Matrix3D::Multiply(inverse, original[i], &offsets[i]);
    }
    world.BeginDeath(body, HitRegion::Chest, -1);
    float worst = 0;
    for (int frame = 0; frame < 60; ++frame) {
        world.Advance(1.f/120.f);
        world.Pose(body, positions, axes);
        std::array<Matrix3D, HumanPartCount> expected;
        Matrix3D inverseRoot;
        root.Get_Orthogonal_Inverse(inverseRoot);
        for (int i = 0; i < HumanPartCount; ++i) {
            Matrix3D relative;
            Matrix3D::Multiply(Transform(positions+i*3, axes+i*9), offsets[i], &expected[i]);
            Matrix3D::Multiply(inverseRoot, expected[i], &relative);
            tree.Capture_Bone(bones[i]);
            tree.Control_Bone_Root_Relative(bones[i], relative);
        }
        tree.Base_Update(root);
        for (int i = 0; i < HumanPartCount; ++i) {
            const Matrix3D &actual = tree.Get_Transform(bones[i]);
            for (int row = 0; row < 3; ++row) for (int column = 0; column < 4; ++column) {
                const float error = std::fabs(actual[row][column]-expected[i][row][column]);
                if (!std::isfinite(error)) return 1;
                if (error > worst) worst=error;
            }
        }
    }
    std::printf("%s native W3D/Jolt binding: %d bones, 60 physics poses, maximum matrix error %.9g\n",
                worst < .00001f ? "PASS" : "FAIL", HumanPartCount, worst);
    return worst < .00001f ? 0 : 1;
}
