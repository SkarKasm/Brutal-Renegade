// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class SoldierGameObj;
class OffenseObjectClass;
class Vector3;

// Shared cosmetic character physics for flatscreen and VR.
class RagdollManager {
public:
#ifdef BR_JOLT_RAGDOLLS
    static void Load_Level();
    static void Unload_Level();
    static void Update(float seconds);
    static void Remove(SoldierGameObj *soldier);
    static void Damaged(SoldierGameObj *soldier, const OffenseObjectClass &damage,
                        const Vector3 &direction, const char *box, float before, float after);
#else
    static void Load_Level() {}
    static void Unload_Level() {}
    static void Update(float) {}
    static void Remove(SoldierGameObj *) {}
    static void Damaged(SoldierGameObj *, const OffenseObjectClass &, const Vector3 &,
                        const char *, float, float) {}
#endif
};
