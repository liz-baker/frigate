#include "level/Frustum.h"

#include <cmath>

namespace Frigate::Level {

namespace {

Vec3 Normalize(const Vec3& v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len <= 0.0f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return { v[0] / len, v[1] / len, v[2] / len };
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float Dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// A plane normal from its local (right, up, basis) weights.
Vec3 Combine(const Vec3& coefficients, const Vec3& right, const Vec3& up, const Vec3& basis) {
    Vec3 out{};
    for (int i = 0; i < 3; i++) {
        out[i] = coefficients[0] * right[i] + coefficients[1] * up[i] + coefficients[2] * basis[i];
    }
    return out;
}

} // namespace

CameraAxes ComputeCameraAxes(const Vec3& look, const Vec3& up) {
    // The basis is the camera's backward axis (it points from the look target
    // back toward the camera), right and up reorthogonalized against it.
    // matrix_4x4_set_basis_and_position: each axis is scaled by the reciprocal of
    // its length, and the up axis is normalized again after the cross product.
    const auto scale = [](const Vec3& v, float factor) { return Vec3{ v[0] * factor, v[1] * factor, v[2] * factor }; };
    const auto lengthSquared = [](const Vec3& v) { return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]); };
    CameraAxes axes;
    axes.basis = scale(look, -1.0f / std::sqrt(lengthSquared(look)));
    const Vec3 right = Cross(up, axes.basis);
    axes.right = scale(right, 1.0f / std::sqrt(lengthSquared(right)));
    const Vec3 realUp = Cross(axes.basis, axes.right);
    axes.up = scale(realUp, 1.0f / std::sqrt(lengthSquared(realUp)));
    return axes;
}

FrustumPlanes ComputeFrustumPlanes(const Vec3& position, const Vec3& look, const Vec3& up, const Vec3& topCoefficients,
                                   const Vec3& leftCoefficients) {
    const CameraAxes axes = ComputeCameraAxes(look, up);
    const Vec3& basis = axes.basis;
    const Vec3& right = axes.right;
    const Vec3& realUp = axes.up;

    FrustumPlanes planes;
    planes.near = basis;
    planes.nearOffset = Dot(basis, position);

    planes.top = Combine(topCoefficients, right, realUp, basis);
    planes.topOffset = Dot(planes.top, position);
    // The bottom plane shares the top plane's weights with the up component
    // flipped (bondviewUpdateFrustumPlanes' nh_div vs. -nh_div).
    const Vec3 bottomCoefficients = { topCoefficients[0], -topCoefficients[1], topCoefficients[2] };
    planes.bottom = Combine(bottomCoefficients, right, realUp, basis);
    planes.bottomOffset = Dot(planes.bottom, position);

    planes.left = Combine(leftCoefficients, right, realUp, basis);
    planes.leftOffset = Dot(planes.left, position);
    // Same relationship for right vs. left, flipping the right component.
    const Vec3 rightCoefficients = { -leftCoefficients[0], leftCoefficients[1], leftCoefficients[2] };
    planes.right = Combine(rightCoefficients, right, realUp, basis);
    planes.rightOffset = Dot(planes.right, position);

    return planes;
}

bool PositionOnScreen(const FrustumPlanes& frustum, const Vec3& pos, float margin) {
    auto passes = [&](const Vec3& normal, float offset) { return offset + margin >= Dot(normal, pos); };
    return passes(frustum.near, frustum.nearOffset) && passes(frustum.left, frustum.leftOffset) &&
           passes(frustum.right, frustum.rightOffset) && passes(frustum.top, frustum.topOffset) &&
           passes(frustum.bottom, frustum.bottomOffset);
}

bool PositionOnScreenBox(const BoxCamera& camera, const Vec3& pos, float margin, const ScreenBox& box) {
    const Mtxf& m = camera.viewToWorld;
    const auto dot = [&](const Vec3& a) { return a[0] * pos[0] + a[1] * pos[1] + a[2] * pos[2]; };
    const auto offsetOf = [&](const Vec3& normal) {
        return normal[0] * m.m[3][0] + normal[1] * m.m[3][1] + normal[2] * m.m[3][2];
    };

    if (camera.nearOffset + margin < m.m[2][0] * pos[0] + m.m[2][1] * pos[1] + m.m[2][2] * pos[2]) {
        return false;
    }

    // Left.
    float slope = (box.minX - camera.screenLeft - camera.halfWidth) * camera.scaleX;
    float invLength = 1.0f / std::sqrt(slope * slope + 1.0f);
    slope *= invLength;
    float negInvLength = -invLength;
    Vec3 normal = { negInvLength * m.m[0][0] - slope * m.m[2][0], negInvLength * m.m[0][1] - slope * m.m[2][1],
                    negInvLength * m.m[0][2] - slope * m.m[2][2] };
    if (offsetOf(normal) + margin < dot(normal)) {
        return false;
    }

    // Right.
    slope = -(box.maxX - camera.screenLeft - camera.halfWidth) * camera.scaleX;
    invLength = 1.0f / std::sqrt(slope * slope + 1.0f);
    slope *= invLength;
    negInvLength = -invLength;
    normal = { -negInvLength * m.m[0][0] - slope * m.m[2][0], -negInvLength * m.m[0][1] - slope * m.m[2][1],
               -negInvLength * m.m[0][2] - slope * m.m[2][2] };
    if (offsetOf(normal) + margin < dot(normal)) {
        return false;
    }

    // Top.
    slope = (camera.halfHeight - (box.minY - camera.screenTop)) * camera.scaleY;
    invLength = 1.0f / std::sqrt(slope * slope + 1.0f);
    slope *= invLength;
    negInvLength = -invLength;
    normal = { -negInvLength * m.m[1][0] + slope * m.m[2][0], -negInvLength * m.m[1][1] + slope * m.m[2][1],
               -negInvLength * m.m[1][2] + slope * m.m[2][2] };
    if (offsetOf(normal) + margin < dot(normal)) {
        return false;
    }

    // Bottom.
    slope = -(camera.halfHeight - (box.maxY - camera.screenTop)) * camera.scaleY;
    invLength = 1.0f / std::sqrt(slope * slope + 1.0f);
    slope *= invLength;
    negInvLength = -invLength;
    normal = { negInvLength * m.m[1][0] + slope * m.m[2][0], negInvLength * m.m[1][1] + slope * m.m[2][1],
               negInvLength * m.m[1][2] + slope * m.m[2][2] };
    if (offsetOf(normal) + margin < dot(normal)) {
        return false;
    }

    return true;
}

} // namespace Frigate::Level
