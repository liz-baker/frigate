#include <cmath>
#include <cstdio>

#include "level/Cctv.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) < tolerance;
}

constexpr float kTau = 6.2831855f;

CctvState MakeCamera() {
    // A camera at the origin looking at a pad along +z, swinging between a
    // twelfth of a turn either side, as quickly as 0x88 allows.
    const Mtxf matrix = IdentityMtxf();
    return MakeCctv(matrix, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 100.0f }, 1.0f,
                    { 5461, -5461, 0x88, -100 });
}

void TestSetup() {
    const CctvState camera = MakeCamera();
    Check(Near(camera.swingA, 5461.0f * kTau / 65536.0f), "the swing's first angle is converted from the file's");
    Check(Near(camera.swingB, -5461.0f * kTau / 65536.0f), "and its second");
    Check(Near(camera.swing, camera.swingA), "the camera starts at the first angle");
    Check(camera.range < 0.0f, "a negative range is no limit");
    Check(Near(camera.lookHeading, std::atan2(0.0f, -100.0f), 1e-3f) || Near(camera.lookHeading, 3.1415927f, 1e-3f),
          "the look heading is that of the pad from the camera");
}

void TestSwingReachesTheEnds() {
    CctvState camera = MakeCamera();
    bool wentBack = false;
    for (int i = 0; i < 20000; i++) {
        SwingCctv(&camera, 1.0f);
        Check(camera.swing >= camera.swingB - 1e-4f && camera.swing <= camera.swingA + 1e-4f,
              "the swing stays between its angles");
        if (camera.swing <= camera.swingB + 1e-6f) {
            wentBack = true;
            break;
        }
    }
    Check(wentBack, "the camera swings round to the second angle");
    Check(camera.towardsA, "and then heads back to the first");
    Check(camera.speed == 0.0f, "stopped at the end");
}

void TestView() {
    CctvState camera = MakeCamera();
    camera.range = 0.0f;
    // Facing along the look heading plus the swing.
    const float facing = camera.swing + camera.lookHeading;
    const Vec3 ahead = { std::sin(facing) * 50.0f, 0.0f, std::cos(facing) * 50.0f };
    const Vec3 behind = { -ahead[0], 0.0f, -ahead[2] };
    Check(CctvInView(camera, behind, false) != CctvInView(camera, ahead, false),
          "the camera sees one side of itself and not the other");
    Check(!CctvInView(camera, ahead, true) && !CctvInView(camera, behind, true), "a camera in motion sees nothing");
    camera.range = 10.0f;
    Check(!CctvInView(camera, ahead, false) && !CctvInView(camera, behind, false), "nothing beyond its range is seen");
}

void TestSwivelPosition() {
    const CctvState camera = MakeCamera();
    const Mtxf matrix = IdentityMtxf();
    const Mtxf swivel = CctvSwivel(camera, matrix, { 10.0f, 20.0f, 30.0f }, { 1.0f, 2.0f, 3.0f });
    Check(Near(swivel.m[3][0], 11.0f) && Near(swivel.m[3][1], 22.0f) && Near(swivel.m[3][2], 33.0f),
          "the swivel node sits on the pivot");
}

} // namespace

int main() {
    TestSetup();
    TestSwingReachesTheEnds();
    TestView();
    TestSwivelPosition();
    if (gFailures == 0) {
        std::printf("CctvTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
