// Copyright (C) 2026 Brutal Renegade contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "physics.h"
#include <array>
#include <cstdint>

namespace BRRagdoll {

enum class HitRegion { Head, Chest, Abdomen, Arm, Leg, Blast };

// One client physics world, advanced by the simulation tick, before either eye renders.
// Handles include a generation so a removed corpse cannot target a reused pool slot.
class World {
public:
    World() = default;
    ~World() { Shutdown(); }
    World(const World &) = delete;
    World &operator=(const World &) = delete;
    using Handle = std::uint32_t;
    static constexpr int MaxBodies = 32;
    static constexpr int MaxActive = 10;

    bool Init();
    void Shutdown();
    void Clear();
    Handle Create(const physPart_t *parts, int count, const unsigned char *noCollide = nullptr);
    void Remove(Handle handle);
    void BeginDeath(Handle handle, HitRegion region, int hitPart);
    void Hit(Handle handle, int part, const float *velocity, const float *point);
    void BulletHit(Handle handle, int part, const float *direction, const float *point, float damage);
    void SetTarget(Handle handle, const float *worldAxes);
    void DisablePart(Handle handle, int part);
    void Advance(float seconds, bool paused = false);
    bool Pose(Handle handle, float *positions, float *axes) const;
    int Count() const;
    int ActiveCount() const;
    std::uint64_t Steps() const { return steps; }

private:
    struct Body {
        int physics = 0;
        int count = 0;
        std::uint32_t generation = 0;
        float age = 0;
        float duration = 0;
        bool dying = false;
        std::array<int, PHYS_MAX_PARTS> parent{};
        std::array<float, PHYS_MAX_PARTS * 9> target{};
        std::array<float, PHYS_MAX_PARTS> stunnedUntil{};
        std::array<bool, PHYS_MAX_PARTS> disabled{};
    };
    Body *Find(Handle handle);
    const Body *Find(Handle handle) const;
    void Drive(Body &body);
    std::array<Body, MaxBodies> bodies{};
    double accumulator = 0;
    std::uint64_t steps = 0;
    bool initialized = false;
};

}
