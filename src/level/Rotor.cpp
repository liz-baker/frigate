#include "level/Rotor.h"

namespace Frigate::Level {

namespace {

// The pose of one node: the rotation about its pivot, then the pivot.
Mtxf Pivot(const Mtxf& rotation, const std::array<float, 3>& origin) {
    Mtxf out = rotation;
    out.m[3][0] = origin[0];
    out.m[3][1] = origin[1];
    out.m[3][2] = origin[2];
    return out;
}

} // namespace

RotorPose PoseRotor(const SwitchGeometry& geometry, float angle, bool inMotion, const Mtxf& body) {
    RotorPose pose;
    pose.matrix[2] =
        MultiplyHomogeneous(body, Pivot(inMotion ? RotationZ(angle) : RotationY(angle), geometry.origin[2]));
    pose.posed[2] = true;
    if (geometry.matrix[3] >= 0) {
        pose.matrix[3] = MultiplyHomogeneous(body, Pivot(RotationX(angle), geometry.origin[3]));
        pose.posed[3] = true;
    }
    if (geometry.matrix[4] >= 0) {
        pose.matrix[4] = MultiplyHomogeneous(body, Pivot(IdentityMtxf(), geometry.origin[4]));
        pose.posed[4] = true;
    }
    return pose;
}

} // namespace Frigate::Level
