#include <cmath>
#include <cstdio>

#include "level/Frustum.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// PlayerCamera::UpdateScale's derivation of TopNormal()/LeftNormal() from a
// half-extent (halfHeight_*scaleY_ or halfWidth_*scaleX_): the local (right,
// up, basis) weights of that plane's normal at unit view depth. Left negates
// its extent first, the way UpdateScale's own local variable does.
Vec3 TopCoefficients(float halfExtent) {
    const float norm = 1.0f / std::sqrt(halfExtent * halfExtent + 1.0f);
    return { 0.0f, norm, halfExtent * norm };
}
Vec3 LeftCoefficients(float halfExtent) {
    const float extent = -halfExtent;
    const float norm = 1.0f / std::sqrt(extent * extent + 1.0f);
    return { -norm, 0.0f, -extent * norm };
}

// A camera at the origin looking down -z with a 90-degree-ish frustum
// (extent 1 on both axes, so the half-angle's tangent is 1).
struct SquareFrustum {
    FrustumPlanes frustum = ComputeFrustumPlanes({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f },
                                                 TopCoefficients(1.0f), LeftCoefficients(1.0f));
};

void TestStraightAheadIsOnScreen() {
    SquareFrustum scene;
    Check(PositionOnScreen(scene.frustum, { 0.0f, 0.0f, -100.0f }, 0.0f), "a point straight ahead is on screen");
}

void TestBehindCameraIsOffScreen() {
    SquareFrustum scene;
    Check(!PositionOnScreen(scene.frustum, { 0.0f, 0.0f, 100.0f }, 0.0f), "a point behind the camera is off screen");
}

void TestWithinHalfAngleIsOnScreen() {
    SquareFrustum scene;
    // At depth 100 the half-angle-1 frustum's half-height/half-width is ~100.
    Check(PositionOnScreen(scene.frustum, { 0.0f, 50.0f, -100.0f }, 0.0f),
          "a point within the vertical half-angle is on screen");
    Check(PositionOnScreen(scene.frustum, { -50.0f, 0.0f, -100.0f }, 0.0f),
          "a point within the horizontal half-angle is on screen");
}

void TestOutsideHalfAngleIsOffScreen() {
    SquareFrustum scene;
    Check(!PositionOnScreen(scene.frustum, { 0.0f, 500.0f, -100.0f }, 0.0f), "a point above the frustum is off screen");
    Check(!PositionOnScreen(scene.frustum, { -500.0f, 0.0f, -100.0f }, 0.0f),
          "a point left of the frustum is off screen");
}

void TestMarginWidensTheFrustum() {
    SquareFrustum scene;
    // Just outside the top plane at depth 100 (half-height there is 100).
    const Vec3 pos = { 0.0f, 101.0f, -100.0f };
    Check(!PositionOnScreen(scene.frustum, pos, 0.0f), "a point just outside the plane fails with no margin");
    Check(PositionOnScreen(scene.frustum, pos, 5.0f), "the same point passes once the margin covers the gap");
}

void TestOffsetCameraPosition() {
    // The same frustum, translated: a point that was on screen relative to
    // the origin should be on screen the same way relative to the camera's
    // new position along its own look direction.
    const Vec3 position = { 10.0f, 20.0f, 30.0f };
    const FrustumPlanes frustum = ComputeFrustumPlanes(position, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f },
                                                       TopCoefficients(1.0f), LeftCoefficients(1.0f));
    Check(PositionOnScreen(frustum, { 10.0f, 20.0f, 30.0f - 100.0f }, 0.0f),
          "a point ahead of a translated camera is on screen");
    Check(!PositionOnScreen(frustum, { 10.0f, 20.0f, 30.0f + 100.0f }, 0.0f),
          "a point behind a translated camera is off screen");
}

void TestUnnormalizedLookMatchesUnitLook() {
    const FrustumPlanes scaled = ComputeFrustumPlanes({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -5.0f }, { 0.0f, 1.0f, 0.0f },
                                                      TopCoefficients(1.0f), LeftCoefficients(1.0f));
    const FrustumPlanes unit = ComputeFrustumPlanes({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f },
                                                    TopCoefficients(1.0f), LeftCoefficients(1.0f));
    Check(PositionOnScreen(scaled, { 0.0f, 50.0f, -100.0f }, 0.0f) ==
              PositionOnScreen(unit, { 0.0f, 50.0f, -100.0f }, 0.0f),
          "a non-unit look vector builds the same frustum as its normalized form");
}

} // namespace

int main() {
    TestStraightAheadIsOnScreen();
    TestBehindCameraIsOffScreen();
    TestWithinHalfAngleIsOnScreen();
    TestOutsideHalfAngleIsOffScreen();
    TestMarginWidensTheFrustum();
    TestOffsetCameraPosition();
    TestUnnormalizedLookMatchesUnitLook();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
