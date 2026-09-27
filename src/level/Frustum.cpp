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

FrustumPlanes ComputeFrustumPlanes(const Vec3& position, const Vec3& look, const Vec3& up, const Vec3& topCoefficients,
                                   const Vec3& leftCoefficients) {
    // matrix_4x4_set_basis_and_position: basis is the camera's backward axis
    // (it points from the look target back toward the camera), right and up
    // reorthogonalized against it the same way.
    const Vec3 basis = Normalize({ -look[0], -look[1], -look[2] });
    const Vec3 right = Normalize(Cross(up, basis));
    const Vec3 realUp = Cross(basis, right);

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

} // namespace Frigate::Level
