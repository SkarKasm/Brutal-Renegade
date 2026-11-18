// Copyright (C) 2026 Brutal Renegade contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "active_ragdoll.h"
#include <algorithm>
#include <cmath>

namespace BRRagdoll {
namespace {
constexpr double Tick = 1.0 / 120.0;
constexpr int MaxStepsPerFrame = 8;
constexpr std::uint32_t SlotBits = 6;
constexpr std::uint32_t SlotMask = (1u << SlotBits) - 1;
World *owner = nullptr;

float Duration(HitRegion region) {
    switch (region) {
        case HitRegion::Head: return 0;
        case HitRegion::Chest: return .9f;
        case HitRegion::Abdomen: return 2.6f;
        case HitRegion::Arm: return 1.6f;
        case HitRegion::Leg: return 2.f;
        case HitRegion::Blast: return 1.f;
    }
    return 0;
}
}

bool World::Init() {
    if (initialized) return true;
    if (owner) return false;
    initialized = Phys_Init(nullptr) != 0;
    if (initialized) owner = this;
    return initialized;
}

void World::Shutdown() {
    if (!initialized) return;
    Clear();
    Phys_Shutdown();
    initialized = false;
    owner = nullptr;
}

void World::Clear() {
    for (Body &body : bodies) {
        if (body.physics) Phys_DestroyRagdoll(body.physics);
        body.physics = 0;
    }
    accumulator = 0;
    steps = 0;
}

World::Handle World::Create(const physPart_t *parts, int count, const unsigned char *noCollide) {
    if (!initialized || !parts || count < 1 || count > PHYS_MAX_PARTS) return 0;
    for (int i = 0; i < count; ++i) {
        // The reused solver creates parent bodies and joints in array order.
        if (parts[i].parent < -1 || parts[i].parent >= i ||
            (i == 0 && parts[i].parent != -1) ||
            !std::isfinite(parts[i].mass) || parts[i].mass <= 0 ||
            !std::isfinite(parts[i].radius) || parts[i].radius <= 0) return 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(parts[i].a[axis]) || !std::isfinite(parts[i].b[axis])) return 0;
        }
    }
    for (std::size_t slot = 0; slot < bodies.size(); ++slot) {
        Body &body = bodies[slot];
        if (body.physics) continue;
        const int physics = Phys_CreateRagdoll(parts, count, noCollide);
        if (!physics) return 0;
        std::uint32_t generation = (body.generation + 1) & (UINT32_MAX >> SlotBits);
        if (!generation) generation = 1;
        body = Body{};
        body.physics = physics;
        body.count = count;
        body.generation = generation;
        for (int i = 0; i < count; ++i) body.parent[i] = parts[i].parent;
        std::array<float, PHYS_MAX_PARTS * 3> positions{};
        Phys_RagdollPose(physics, positions.data(), body.target.data());
        return (generation << SlotBits) | static_cast<Handle>(slot + 1);
    }
    return 0;
}

World::Body *World::Find(Handle handle) {
    return const_cast<Body *>(static_cast<const World *>(this)->Find(handle));
}

const World::Body *World::Find(Handle handle) const {
    const std::uint32_t index = handle & SlotMask;
    if (index == 0 || index > bodies.size()) return nullptr;
    const Body &body = bodies[index - 1];
    return body.physics && body.generation == (handle >> SlotBits) ? &body : nullptr;
}

void World::Remove(Handle handle) {
    Body *body = Find(handle);
    if (!body) return;
    Phys_DestroyRagdoll(body->physics);
    body->physics = 0;
}

void World::BeginDeath(Handle handle, HitRegion region, int hitPart) {
    Body *body = Find(handle);
    if (!body || body->dying) return;
    body->duration = ActiveCount() < MaxActive ? Duration(region) : 0;
    body->age = 0;
    body->dying = true;
    if (hitPart >= 0 && hitPart < body->count) body->stunnedUntil[hitPart] = .3f;
    Drive(*body);
}

void World::Hit(Handle handle, int part, const float *velocity, const float *point) {
    Body *body = Find(handle);
    if (!body || !velocity || !point || part < 0 || part >= body->count) return;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(velocity[axis]) || !std::isfinite(point[axis])) return;
    }
    body->stunnedUntil[part] = body->age + .3f;
    Phys_RagdollAddVelocityAt(body->physics, part, velocity, point);
}

void World::SetTarget(Handle handle, const float *axes) {
    Body *body = Find(handle);
    if (!body || !axes) return;
    for (int i = 0; i < body->count * 9; ++i) {
        if (!std::isfinite(axes[i])) return;
    }
    std::copy(axes, axes + body->count * 9, body->target.begin());
}

void World::DisablePart(Handle handle, int part) {
    Body *body = Find(handle);
    if (!body || part < 0 || part >= body->count) return;
    if (body->disabled[part]) return;
    body->disabled[part] = true;
    // Parent order lets this propagate across an entire severed branch in one pass.
    for (int i = part + 1; i < body->count; ++i) {
        if (body->parent[i] >= 0 && body->disabled[body->parent[i]]) body->disabled[i] = true;
    }
    Drive(*body);
}

void World::Drive(Body &body) {
    if (!body.dying || body.duration <= body.age) {
        Phys_RagdollDrive(body.physics, nullptr, nullptr, 0);
        return;
    }
    std::array<float, PHYS_MAX_PARTS> strength{};
    const float fade = 1.f - body.age / body.duration;
    for (int i = 1; i < body.count; ++i) {
        if (!body.disabled[i] && body.stunnedUntil[i] <= body.age) strength[i] = fade * fade;
    }
    Phys_RagdollDrive(body.physics, body.target.data(), strength.data(), std::min(body.age / .15f, 1.f));
}

void World::Advance(float seconds, bool paused) {
    if (!initialized || paused || !std::isfinite(seconds) || seconds <= 0) return;
    // Bound catch-up work after a hitch and retain fractional ticks for stereo consistency.
    accumulator += std::min(static_cast<double>(seconds), Tick * MaxStepsPerFrame);
    int ticks = 0;
    while (accumulator + 1e-9 >= Tick && ticks++ < MaxStepsPerFrame) {
        for (Body &body : bodies) {
            if (!body.physics) continue;
            body.age += static_cast<float>(Tick);
            Drive(body);
        }
        Phys_Step(static_cast<float>(Tick), 9.81f);
        accumulator -= Tick;
        ++steps;
    }
}

bool World::Pose(Handle handle, float *positions, float *axes) const {
    const Body *body = Find(handle);
    if (!body || !positions || !axes) return false;
    Phys_RagdollPose(body->physics, positions, axes);
    return true;
}

int World::Count() const {
    return static_cast<int>(std::count_if(bodies.begin(), bodies.end(), [](const Body &body) { return body.physics != 0; }));
}

int World::ActiveCount() const {
    return static_cast<int>(std::count_if(bodies.begin(), bodies.end(), [](const Body &body) {
        return body.physics && body.dying && body.age < body.duration;
    }));
}
}
