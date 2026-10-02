#include "level/Tank.h"

#include <cmath>

#include "level/Objects.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kHalfPi = 1.5707964f;

// A rotation with the origin of a model node as its position.
Mtxf Pivot(Mtxf rotation, const std::array<float, 3>& origin) {
    for (size_t axis = 0; axis < 3; axis++) {
        rotation.m[3][axis] = origin[axis];
    }
    return rotation;
}

} // namespace

Vec3 TankModelOffset(const TankGeometry& geometry, float turretOrientation) {
    const Mtxf rotation = RotationY(kTau - turretOrientation);
    Vec3 offset = RotateVector(rotation, { geometry.nodes[1][0], geometry.nodes[1][1], geometry.nodes[1][2] });
    for (size_t axis = 0; axis < 3; axis++) {
        offset[axis] += geometry.nodes[0][axis];
    }
    return offset;
}

std::array<Vec2, 8> TankCollisionBounds(const ModelBounds& hull, const Vec3& modelOffset, float scale,
                                        const Vec3& position, float orientation) {
    const float xmin = hull.xmin - modelOffset[0];
    const float xmax = hull.xmax - modelOffset[0];
    const float zmin = hull.zmin - modelOffset[2];
    const float zmax = hull.zmax - modelOffset[2];

    const float cosine = scale * GameCosf(orientation);
    const float sine = scale * GameSinf(orientation);

    std::array<Vec2, 8> polygon{};
    polygon[0] = { position[0] + (-zmin * sine) + (xmin * cosine), position[2] + (zmin * cosine) + (xmin * sine) };
    polygon[1] = { position[0] + (-zmin * sine) + (xmax * cosine), position[2] + (zmin * cosine) + (xmax * sine) };
    polygon[2] = { position[0] + (-zmax * sine) + (xmax * cosine), position[2] + (zmax * cosine) + (xmax * sine) };
    polygon[3] = { position[0] + (-zmax * sine) + (xmin * cosine), position[2] + (zmax * cosine) + (xmin * sine) };
    return polygon;
}

std::array<Mtxf, 5> TankPose(const TankGeometry& geometry, const TankState& state, const Mtxf& base) {
    float pitch = -state.turretVerticalAngle;
    if (pitch < 0.0f) {
        pitch += kTau;
    }
    float heading = -state.turretOrientationAngle;
    if (heading < 0.0f) {
        heading += kTau;
    }

    std::array<Mtxf, 5> pose{};
    pose[0] = base;
    pose[1] = Multiply(base, Pivot(RotationY(heading), geometry.nodes[0]));
    pose[3] = Multiply(pose[1], Pivot(RotationX(pitch), geometry.nodes[2]));
    pose[4] = Multiply(pose[3], Pivot(RotationY(kHalfPi), geometry.nodes[3]));
    pose[2] = Multiply(pose[1], Pivot(IdentityMtxf(), geometry.nodes[1]));
    return pose;
}

void UpdateTankRect(const TankGeometry& geometry, TankState* state, const Mtxf& worldBase) {
    const std::array<Mtxf, 5> pose = TankPose(geometry, *state, worldBase);
    ModelBounds top;
    top.xmin = geometry.top[0];
    top.xmax = geometry.top[1];
    top.ymin = geometry.top[2];
    top.ymax = geometry.top[3];
    top.zmin = geometry.top[4];
    top.zmax = geometry.top[5];

    CollisionBlock block;
    BuildCollisionPolygon(top, pose[1], &block);
    state->rect = block.polygon;
    state->rectEdges = block.edges;
}

} // namespace Frigate::Level
