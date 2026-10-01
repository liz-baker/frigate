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

// A 320 by 240 view from the origin looking down -z, a scale of 0.005 a pixel.
BoxCamera MakeBoxCamera() {
    BoxCamera camera;
    for (int i = 0; i < 4; i++) {
        camera.viewToWorld.m[i][i] = 1.0f;
    }
    camera.halfWidth = 160.0f;
    camera.halfHeight = 120.0f;
    camera.scaleX = 0.005f;
    camera.scaleY = 0.005f;
    return camera;
}

void TestWholeScreenBoxMatchesTheView() {
    const BoxCamera camera = MakeBoxCamera();
    const ScreenBox whole = { 0.0f, 0.0f, 320.0f, 240.0f };
    Check(PositionOnScreenBox(camera, { 0.0f, 0.0f, -100.0f }, 0.0f, whole), "straight ahead is in the whole screen");
    Check(!PositionOnScreenBox(camera, { 0.0f, 0.0f, 100.0f }, 0.0f, whole), "behind the camera is not");
    // At depth 100 the view is 0.8 * 100 = 80 units either way from the centre
    // (a 160 pixel half width at 0.005 a pixel).
    Check(PositionOnScreenBox(camera, { 70.0f, 0.0f, -100.0f }, 0.0f, whole), "inside the edge");
    Check(!PositionOnScreenBox(camera, { 90.0f, 0.0f, -100.0f }, 0.0f, whole), "outside the edge");
    Check(!PositionOnScreenBox(camera, { -90.0f, 0.0f, -100.0f }, 0.0f, whole), "outside the other");
    Check(!PositionOnScreenBox(camera, { 0.0f, 70.0f, -100.0f }, 0.0f, whole), "above the view's 60 unit half height");
}

void TestNarrowBoxRejectsWhatIsBesideIt() {
    const BoxCamera camera = MakeBoxCamera();
    // The middle tenth of the screen: 16 by 12 pixels.
    const ScreenBox middle = { 152.0f, 114.0f, 168.0f, 126.0f };
    Check(PositionOnScreenBox(camera, { 0.0f, 0.0f, -100.0f }, 0.0f, middle), "the centre is in the middle");
    Check(!PositionOnScreenBox(camera, { 20.0f, 0.0f, -100.0f }, 0.0f, middle), "a point to one side is not");
    Check(!PositionOnScreenBox(camera, { -20.0f, 0.0f, -100.0f }, 0.0f, middle), "nor to the other");
    Check(!PositionOnScreenBox(camera, { 0.0f, 20.0f, -100.0f }, 0.0f, middle), "nor above");
    Check(!PositionOnScreenBox(camera, { 0.0f, -20.0f, -100.0f }, 0.0f, middle), "nor below");
    Check(PositionOnScreenBox(camera, { 20.0f, 0.0f, -100.0f }, 20.0f, middle), "until the margin covers it");
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
    TestWholeScreenBoxMatchesTheView();
    TestNarrowBoxRejectsWhatIsBesideIt();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
