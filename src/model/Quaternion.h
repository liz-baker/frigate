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

// Negates `q2` if it's more than 90 degrees from `q1` (their dot product is
// negative), so a subsequent Slerp takes the shorter of the two paths
// between them. Transcribed from `quaternion_ensure_shortest_path`.
Quaternion EnsureShortestPath(const Quaternion& q1, const Quaternion& q2);

// General slerp from `q1` toward `q2` by fraction `t`. The same three-branch
// shape as SlerpFromIdentity (near-antipodal linear fallback, the usual
// sin-weighted blend, near-identical linear lerp), generalized to any two
// quaternions rather than from identity. Transcribed from
// `quaternion_slerp`. Callers that need the shortest path (as
// `process_02_position` always does) call EnsureShortestPath first.
Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t);

} // namespace Frigate
