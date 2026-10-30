#pragma once

#include "level/Matrix.h"

namespace Frigate::Level {

// bondviewUpdateFrustumPlanes's five frustum planes (near, left, right, top,
// bottom), each a world-space normal and the offset a position's dot product
// with it is compared against.
struct FrustumPlanes {
    Vec3 nearPlane{}; // not `near`: windows.h defines that as a macro
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

// The camera's world-space axes (matrix_4x4_set_basis_and_position): `basis`
// points back toward the camera from its look direction, `right` and `up` are
// orthogonal to it.
struct CameraAxes {
    Vec3 right{};
    Vec3 up{};
    Vec3 basis{};
};
CameraAxes ComputeCameraAxes(const Vec3& look, const Vec3& up);

// bondviewUpdateFrustumPlanes: the frustum built from a camera at `position`
// looking along `look` with `up`, using PlayerCamera::UpdateScale's
// precomputed top/left plane coefficients (its TopNormal()/LeftNormal(): the
// local (right, up, basis) weights of the top and left plane normals at unit
// view depth -- bottom and right are the same weights with one axis flipped).
FrustumPlanes ComputeFrustumPlanes(const Vec3& position, const Vec3& look, const Vec3& up, const Vec3& topCoefficients,
                                   const Vec3& leftCoefficients);

// An axis-aligned rectangle of the screen, in pixels (bbox2d: min then max).
struct ScreenBox {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

// What camIsPosInScreenBox reads of the current player: the view-to-world
// matrix (rows 0 to 2 the camera's right, up and backward axes, row 3 its
// position) and the fields of its projection.
struct BoxCamera {
    Mtxf viewToWorld{};
    // g_CamFrustumNearOffset.
    float nearOffset = 0.0f;
    // c_screenleft, c_screentop, c_halfwidth, c_halfheight, c_scalex, c_scaley.
    float screenLeft = 0.0f;
    float screenTop = 0.0f;
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    float scaleX = 0.0f;
    float scaleY = 0.0f;
};

// camIsPosInScreen: whether a sphere of `margin` radius centred at `pos`
// intersects the frustum (true if it isn't entirely outside any one plane).
bool PositionOnScreen(const FrustumPlanes& frustum, const Vec3& pos, float margin);

// camIsPosInScreenBox: as PositionOnScreen, against the planes through the
// edges of `box` rather than the whole view's.
bool PositionOnScreenBox(const BoxCamera& camera, const Vec3& pos, float margin, const ScreenBox& box);

} // namespace Frigate::Level
