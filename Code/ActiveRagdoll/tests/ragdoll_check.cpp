// SPDX-License-Identifier: GPL-3.0-or-later
#include "active_ragdoll.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

static int failures;
static void Check(bool result, const char *message) {
    std::printf("%s %s\n", result ? "PASS" : "FAIL", message);
    if (!result) ++failures;
}

static std::array<physPart_t, 2> Parts(float x = 0) {
    std::array<physPart_t, 2> parts{};
    for (int i = 0; i < 2; ++i) {
        physPart_t &part = parts[i];
        part.parent = i - 1;
        part.shape = PHYS_SHAPE_CAPSULE;
        part.a[0] = part.b[0] = x;
        part.a[2] = 2.f + i * .4f;
        part.b[2] = part.a[2] + .4f;
        part.radius = .08f;
        part.mass = i ? 3.f : 8.f;
        part.joint = i ? PHYS_JOINT_SWINGTWIST : PHYS_JOINT_NONE;
        part.pivot[0] = x;
        part.pivot[2] = part.a[2];
        part.twist1[2] = part.twist2[2] = 1;
        part.plane[0] = 1;
        part.planeHalf = part.normalHalf = 70;
        part.twistMin = -45;
        part.twistMax = 45;
    }
    return parts;
}

static void Floor() {
    const float points[] = {-30,-30,-1, 30,-30,-1, -30,30,-1, 30,30,-1,
                           -30,-30,0, 30,-30,0, -30,30,0, 30,30,0};
    Phys_BeginWorld();
    Phys_AddWorldHull(points, 8);
    Phys_EndWorld();
}

int main() {
    using namespace BRRagdoll;
    std::printf("Process pointer width %zu bits\n", sizeof(void *) * 8);
    Check(sizeof(void *) == 4, "same 32-bit architecture as Renegade");
    World world;
    Check(world.Init(), "Jolt initializes");
    World second;
    Check(!second.Init(), "another owner cannot reset an existing physics world");
    Floor();
    auto parts = Parts();
    const auto body = world.Create(parts.data(), 2);
    Check(body != 0, "two connected capsule bodies created in metres");
    world.BeginDeath(body, HitRegion::Abdomen, 1);
    Check(world.ActiveCount() == 1, "abdomen death enables pose motors");
    std::array<float, PHYS_MAX_PARTS * 3> pos{};
    std::array<float, PHYS_MAX_PARTS * 9> axes{};
    Check(world.Pose(body, pos.data(), axes.data()), "pose can be read");
    const float startZ = pos[2];
    world.Advance(1.f / 120.f);
    world.Pose(body, pos.data(), axes.data());
    const float drop = startZ - pos[2];
    Check(drop > .0001f && drop < .003f, "gravity uses metres per second squared");
    const auto beforePause = world.Steps();
    world.Advance(1, true);
    world.Advance(std::numeric_limits<float>::quiet_NaN());
    Check(world.Steps() == beforePause, "pause and invalid delta do not advance simulation");
    world.Advance(10);
    Check(world.Steps() - beforePause == 8, "a long hitch has bounded catch-up work");
    const float velocity[] = {2, 0, 0};
    const float point[] = {0, 0, 2.6f};
    world.Hit(body, 1, velocity, point);
    for (int frame = 0; frame < 240; ++frame) world.Advance(1.f / 60.f);
    world.Pose(body, pos.data(), axes.data());
    Check(world.ActiveCount() == 0, "death motors fade to a passive ragdoll");
    bool finite = true;
    for (int i = 0; i < 6; ++i) finite = finite && std::isfinite(pos[i]);
    Check(finite && pos[2] > -.2f && pos[5] > -.2f && pos[2] < 1 && pos[5] < 1,
          "shot ragdoll settles against the floor without tunnelling");
    world.Remove(body);
    const auto replacement = world.Create(parts.data(), 2);
    Check(replacement != body && !world.Pose(body, pos.data(), axes.data()),
          "stale handles cannot affect a replacement corpse");
    world.Remove(body);
    Check(world.Count() == 1, "stale remove leaves the replacement alive");
    world.Clear();
    Check(world.Count() == 0, "level clear removes all ragdolls");
    int physicsBodies, activeBodies, hulls, triangles;
    Phys_Stats(&physicsBodies, &activeBodies, &hulls, &triangles);
    Check(physicsBodies == 1, "level clear releases physics bodies and retains only terrain");

    // A real motor test: ask the child capsule to bend while both bodies free-fall.
    parts = Parts();
    for (auto &part : parts) { part.a[2] += 10; part.b[2] += 10; part.pivot[2] += 10; }
    const auto driven = world.Create(parts.data(), 2);
    world.Pose(driven, pos.data(), axes.data());
    const float angle = .6f;
    for (int column = 0; column < 3; ++column) {
        const int base = 9 + column * 3;
        const float y = axes[base + 1], z = axes[base + 2];
        axes[base + 1] = std::cos(angle) * y - std::sin(angle) * z;
        axes[base + 2] = std::sin(angle) * y + std::cos(angle) * z;
    }
    world.SetTarget(driven, axes.data());
    world.BeginDeath(driven, HitRegion::Abdomen, -1);
    for (int tick = 0; tick < 60; ++tick) world.Advance(1.f / 120.f);
    world.Pose(driven, pos.data(), axes.data());
    float trace = 0;
    for (int i = 0; i < 9; ++i) trace += axes[i] * axes[i+9];
    const float cosine = (trace - 1) * .5f;
    const float bend = std::acos(cosine < -1 ? -1 : cosine > 1 ? 1 : cosine);
    std::printf("Motor target bend %.3f actual %.3f radians\n", angle, bend);
    Check(bend > .3f && bend < .9f, "joint motor physically follows the animation target");
    world.Clear();

    std::array<World::Handle, World::MaxBodies> handles{};
    for (int i = 0; i < World::MaxBodies; ++i) {
        parts = Parts(static_cast<float>(i));
        handles[i] = world.Create(parts.data(), 2);
        world.BeginDeath(handles[i], HitRegion::Chest, 1);
    }
    Check(world.Count() == World::MaxBodies && world.ActiveCount() == World::MaxActive,
          "32 corpse slots and ten motor-driven corpses bound simulation cost");
    Check(!world.Create(parts.data(), 2), "capacity exhaustion preserves existing bodies");
    world.Clear();
    parts[1].parent = 1;
    Check(!world.Create(parts.data(), 2), "invalid joint topology is rejected before Jolt");
    parts = Parts();
    const auto headshot = world.Create(parts.data(), 2);
    world.BeginDeath(headshot, HitRegion::Head, 1);
    Check(world.ActiveCount() == 0, "headshot goes limp immediately");
    world.Shutdown();
    Check(!Phys_Active(), "shutdown releases the physics world");
    Check(world.Init(), "physics can initialize again after level teardown");
    world.Shutdown();
    return failures ? 1 : 0;
}
