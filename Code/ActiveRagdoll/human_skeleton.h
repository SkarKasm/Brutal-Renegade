// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace BRRagdoll {
struct Segment {
    const char *bone;
    const char *tip;
    int parent;
    float radius;
    float mass;
};
inline constexpr Segment HumanSegments[] = {
    {"C PELVIS", "C SPINE1", -1, .13f, 12.f},
    {"C SPINE1", "C NECK", 0, .15f, 20.f},
    {"C HEAD", nullptr, 1, .10f, 5.f},
    {"C L UPPERARM", "C L FOREARM", 1, .065f, 3.f},
    {"C L FOREARM", "C L HAND", 3, .055f, 2.f},
    {"C R UPPERARM", "C R FOREARM", 1, .065f, 3.f},
    {"C R FOREARM", "C R HAND", 5, .055f, 2.f},
    {"C L THIGH", "C L CALF", 0, .09f, 8.f},
    {"C L CALF", "C L FOOT", 7, .065f, 4.f},
    {"C R THIGH", "C R CALF", 0, .09f, 8.f},
    {"C R CALF", "C R FOOT", 9, .065f, 4.f}
};
inline constexpr int HumanPartCount = sizeof(HumanSegments) / sizeof(HumanSegments[0]);
}
