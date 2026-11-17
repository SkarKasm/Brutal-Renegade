#ifndef VR_FOREARM_H
#define VR_FOREARM_H

#include "vector3.h"
#include "vector2.h"
#include "wwmath.h"

inline float VR_EVA_Mount_Angle(float x)
{
    // Move the EVA band toward the inside of the left arm. Blend out the
    // rotation before the elbow and wrist, where the other meshes join it.
    const float weight = WWMath::Clamp(WWMath::Min((x-.012f)/.038f,(.263f-x)/.045f),0.f,1.f);
    return DEG_TO_RADF(47.f)*weight*weight*(3.f-2.f*weight);
}

inline Vector3 VR_Rotate_Forearm_X(const Vector3 &v,float angle)
{
    const float c = cosf(angle), s = sinf(angle);
    return Vector3(v.X,c*v.Y-s*v.Z,s*v.Y+c*v.Z);
}

struct VREvaVertex
{
    Vector3 Position;
    Vector2 UV;
};

#endif
