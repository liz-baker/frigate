#include "level/VehicleWheels.h"

#include <cmath>

#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;

void Wrap(float* angle) {
    while (*angle >= kTau) {
        *angle -= kTau;
    }
    while (*angle < 0.0f) {
        *angle += kTau;
    }
}

Mtxf Pivot(Mtxf rotation, const std::array<float, 3>& origin) {
    for (size_t axis = 0; axis < 3; axis++) {
        rotation.m[3][axis] = origin[axis];
    }
    return rotation;
}

} // namespace

void StepVehicleWheels(ObjectAi& ai, const VehicleGeometry& geometry, float scale, float delta, bool isOwner) {
    float spin = 0.0f;
    // The wheel's size is the height of the box in slot 6.
    float size = (geometry.hull[3] - geometry.hull[2]) * scale;
    if (isOwner) {
        spin = ((ai.speed * delta) * kTau) / ((size * kTau) * 0.5f);
        ai.wheelXRot += spin;
        Wrap(&ai.wheelXRot);
    }
    ai.wheelXRot += spin;
    Wrap(&ai.wheelXRot);

    if (ai.speed > 0.0f) {
        float turn = ai.turnRate;
        size = (geometry.points[2][2] - geometry.points[0][2]) * scale;
        if (turn < 0.0f) {
            turn = -turn;
        }
        const float side = GameSinf(turn) * size;
        ai.wheelYRot = std::atan2(side, (GameCosf(turn) * size) - (size - ai.speed));
        if (ai.wheelYRot < turn) {
            ai.wheelYRot = turn;
        }
        if (ai.turnRate > 0.0f) {
            ai.wheelYRot = kTau - ai.wheelYRot;
        }
    }
}

std::array<Mtxf, 5> VehicleWheelPose(const VehicleGeometry& geometry, const ObjectAi& ai, const Mtxf& base) {
    const Mtxf spin = RotationX(ai.wheelXRot);
    const Mtxf front = MultiplyHomogeneous(RotationY(ai.wheelYRot), spin);

    std::array<Mtxf, 5> pose{};
    pose[0] = base;
    pose[1] = MultiplyHomogeneous(base, Pivot(front, geometry.points[0]));
    pose[2] = MultiplyHomogeneous(base, Pivot(front, geometry.points[1]));
    pose[3] = MultiplyHomogeneous(base, Pivot(spin, geometry.points[2]));
    pose[4] = MultiplyHomogeneous(base, Pivot(spin, geometry.points[3]));
    return pose;
}

} // namespace Frigate::Level
