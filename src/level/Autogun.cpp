#include "level/Autogun.h"

#include <cmath>

#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kHalfPi = 1.5707964f;

// The pose of one node: the rotation about its pivot, then the pivot.
Mtxf Pivot(const Mtxf& rotation, const std::array<float, 3>& origin) {
    Mtxf out = rotation;
    out.m[3][0] = origin[0];
    out.m[3][1] = origin[1];
    out.m[3][2] = origin[2];
    return out;
}

} // namespace

AutogunState MakeAutogun(const Vec3& position, const Vec3* padPosition, const std::array<int32_t, 4>& fixed) {
    AutogunState state;
    state.limitMax = (static_cast<float>(fixed[0]) * kTau) / 65536.0f;
    state.limitMin = (static_cast<float>(fixed[1]) * kTau) / 65536.0f;
    state.speed = (static_cast<float>(fixed[2]) * kTau) / 65536.0f;
    state.aimDistance = (static_cast<float>(fixed[3]) * 100.0f) / 65536.0f;

    if (padPosition != nullptr) {
        const float xdiff = (*padPosition)[0] - position[0];
        const float ydiff = (*padPosition)[1] - position[1];
        const float zdiff = (*padPosition)[2] - position[2];
        state.restYaw = GameAtan2f(xdiff, zdiff);
        state.restPitch = GameAtan2f(ydiff, std::sqrt((xdiff * xdiff) + (zdiff * zdiff)));
    }
    return state;
}

AutogunPose PoseAutogun(const SwitchGeometry& geometry, const AutogunState& state, const Mtxf& base, float scale) {
    AutogunPose pose;
    float heading = state.yaw + kHalfPi;
    const float pitch = -state.pitch;
    if (heading >= kTau) {
        heading -= kTau;
    }

    // The heading turns about the pivot of slot 1, which sits where the body's
    // matrix puts it; the model's scale applies to the rotation.
    const Vec3 pivot = TransformVector(base, { geometry.origin[1][0], geometry.origin[1][1], geometry.origin[1][2] });
    Mtxf turned = RotationY(heading);
    turned.m[3][0] = pivot[0];
    turned.m[3][1] = pivot[1];
    turned.m[3][2] = pivot[2];
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            turned.m[row][column] *= scale;
        }
    }
    pose.matrix[1] = turned;
    pose.posed[1] = true;

    pose.matrix[2] = MultiplyHomogeneous(turned, Pivot(RotationZ(pitch), geometry.origin[2]));
    pose.posed[2] = true;

    // The barrels spin about x; the part between them does not move.
    for (const size_t slot : { size_t{ 3 }, size_t{ 6 } }) {
        if (geometry.matrix[slot] >= 0) {
            pose.matrix[slot] =
                MultiplyHomogeneous(pose.matrix[2], Pivot(RotationX(state.spinAngle), geometry.origin[slot]));
            pose.posed[slot] = true;
        }
    }
    if (geometry.matrix[4] >= 0) {
        pose.matrix[4] = MultiplyHomogeneous(pose.matrix[2], Pivot(IdentityMtxf(), geometry.origin[4]));
        pose.posed[4] = true;
    }
    return pose;
}

Mtxf AutogunNodeMatrix(const SwitchGeometry& geometry, const AutogunPose& pose, const Mtxf& base, size_t slot) {
    const int index = geometry.matrix[slot];
    if (index == 1 || index == 2) {
        return pose.matrix[static_cast<size_t>(index)];
    }
    for (const size_t owner : { size_t{ 3 }, size_t{ 4 }, size_t{ 6 } }) {
        if (pose.posed[owner] && geometry.matrix[owner] == index) {
            return pose.matrix[owner];
        }
    }
    return base;
}

} // namespace Frigate::Level
