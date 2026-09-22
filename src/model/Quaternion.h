#pragma once

#include "model/Animation.h"

// Quaternion helpers for GoldenEye's bend-node (matrix1) and
// secondary-animation-blend rotation paths. Transcribed from the GoldenEye
// decomp's quaternion.c.
namespace Frigate {

struct Quaternion {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Half-angle XYZ Euler to quaternion. Transcribed from
// `quaternion_set_rotation_around_xyzf`.
Quaternion EulerToQuaternion(const JointRotation& rot);

// Slerp from the identity quaternion toward `q` by fraction `t` -- e.g.
// t=0.5 is "half of q's own rotation". Transcribed from
// `quaternion_7F05BC68`; its "near-antipodal" branch is omitted as
// unreachable here (the decomp's own version mirrors `quaternion_slerp`'s
// three-branch shape, but slerping from the identity can only ever land in
// the other two).
Quaternion SlerpFromIdentity(const Quaternion& q, float t);

} // namespace Frigate
