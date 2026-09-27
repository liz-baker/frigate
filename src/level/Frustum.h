#pragma once

#include "level/Matrix.h"

namespace Frigate::Level {

// bondviewUpdateFrustumPlanes's five frustum planes (near, left, right, top,
// bottom), each a world-space normal and the offset a position's dot product
// with it is compared against.
struct FrustumPlanes {
    Vec3 near{};
    float nearOffset = 0.0f;
    Vec3 left{};
    float leftOffset = 0.0f;
    Vec3 right{};
    float rightOffset = 0.0f;
    Vec3 top{};
    float topOffset = 0.0f;
    Vec3 bottom{};
    float bottomOffset = 0.0f;
};

// matrix_4x4_set_basis_and_position (the camera's world-space right/up/basis
// axes, `basis` pointing back toward the camera from its look direction) plus
// bondviewUpdateFrustumPlanes: the frustum built from a camera at `position`
// looking along `look` with `up`, using PlayerCamera::UpdateScale's
// precomputed top/left plane coefficients (its TopNormal()/LeftNormal(): the
// local (right, up, basis) weights of the top and left plane normals at unit
// view depth -- bottom and right are the same weights with one axis flipped).
FrustumPlanes ComputeFrustumPlanes(const Vec3& position, const Vec3& look, const Vec3& up, const Vec3& topCoefficients,
                                   const Vec3& leftCoefficients);

// camIsPosInScreen: whether a sphere of `margin` radius centred at `pos`
// intersects the frustum (true if it isn't entirely outside any one plane).
bool PositionOnScreen(const FrustumPlanes& frustum, const Vec3& pos, float margin);

} // namespace Frigate::Level
