#include <cmath>
#include <cstdio>

#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "level/Fog.h"
#include "level/Sky.h"

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
    return std::fabs(a - b) <= tolerance * std::max(1.0f, std::fabs(b));
}

// A 320 by 240 view with a 60 degree vertical field, from a camera looking
// down -z.
SkyView MakeView() {
    SkyView view;
    view.view = { 320, 240, 0, 0, 320, 240 };
    view.halfWidth = 160.0f;
    view.halfHeight = 120.0f;
    const float tangent = std::sin(30.0f * 3.14159265f / 180.0f) / std::cos(30.0f * 3.14159265f / 180.0f);
    view.scaleY = tangent / 120.0f;
    view.scaleX = view.scaleY * (4.0f / 3.0f) * 120.0f / 160.0f;
    for (int i = 0; i < 4; i++) {
        view.viewToWorld.m[i][i] = 1.0f;
    }
    return view;
}

void TestRayThroughTheViewCentre() {
    const Vec3 ray = SkyMath::WorldPosFromScreenPos(MakeView(), 160.0f, 120.0f, 0.0f);
    Check(Near(ray[0], 0.0f) && Near(ray[1], 0.0f) && Near(ray[2], -100.0f), "straight ahead, a hundred units long");

    const Vec3 corner = SkyMath::WorldPosFromScreenPos(MakeView(), 0.0f, 0.0f, 0.0f);
    Check(corner[0] < 0.0f && corner[1] > 0.0f && corner[2] < 0.0f, "the top left corner looks up and to the left");
    Check(Near(std::sqrt(corner[0] * corner[0] + corner[1] * corner[1] + corner[2] * corner[2]), 100.0f),
          "and is a hundred units long too");
}

void TestCloudPlane() {
    const Vec3 eye = { 0.0f, 0.0f, 0.0f };
    const SkyMath::Corner up = SkyMath::CornerInSky(eye, { 0.0f, 50.0f, -50.0f }, 1000.0f);
    Check(up.valid && Near(up.position[1], 1000.0f) && Near(up.position[2], -1000.0f), "a ray up meets the plane");
    Check(up.fraction == 0.0f, "well clear of the horizon");

    const SkyMath::Corner down = SkyMath::CornerInSky(eye, { 0.0f, -50.0f, -50.0f }, 1000.0f);
    Check(!down.valid, "a ray down does not");

    // Level with the horizon the ray is tilted up by a hair, and the distance it
    // travels is held to 300000.
    const SkyMath::Corner level = SkyMath::CornerInSky(eye, { 0.0f, 0.0f, -50.0f }, 1000.0f);
    Check(level.valid && Near(level.position[2], -300000.0f, 1e-3f), "at the horizon the plane is 300000 units off");
    Check(Near(level.fraction, 1.0f), "and the corner is all horizon");
}

void TestWaterPlane() {
    const Vec3 eye = { 0.0f, 0.0f, 0.0f };
    const SkyMath::Corner down = SkyMath::CornerInWater(eye, { 0.0f, -50.0f, -50.0f }, -1000.0f);
    Check(down.valid && Near(down.position[1], -1000.0f) && Near(down.position[2], -1000.0f),
          "a ray down meets the sea");
    Check(!SkyMath::CornerInWater(eye, { 0.0f, 50.0f, -50.0f }, -1000.0f).valid, "a ray up does not");
}

void TestEdgeVertex() {
    const Vec3 edge = SkyMath::EdgeVertex({ 0.0f, 10.0f, 0.0f }, { 10.0f, -10.0f, 20.0f });
    Check(edge[0] == 5.0f && edge[1] == 0.0f && edge[2] == 10.0f,
          "half way between rays of equal height above and below");
}

void TestStageWithoutCloudsIsFilled() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Facility, false, 1, 1.0f);
    Check(fog.Environment().sky.clouds == 0, "Facility has no clouds");

    Sky sky;
    Gfx buffer[16] = {};
    const Gfx* end = sky.Render(buffer, nullptr, MakeView(), fog);
    Check(end - buffer == 4, "it is filled with four commands");
    // Fill cycle, fill colour, the view's rectangle, and a pipe sync.
    Check((buffer[0].words.w0 & 0xFFFF) == ((G_MDSFT_CYCLETYPE << 8) | 2) && buffer[0].words.w1 == G_CYC_FILL,
          "in the fill cycle");
    // The fog colour (0x10, 0x20, 0x10) in 5 bit channels, twice.
    const uint32_t pixel = (2u << 11) | (4u << 6) | (2u << 1) | 1u;
    Check(buffer[1].words.w1 == ((pixel << 16) | pixel), "with the fog's colour");
    Check((buffer[2].words.w0 >> 24) == 0xF6, "over a rectangle");
}

void TestTickScrollsTheClouds() {
    Sky sky;
    sky.Tick(2);
    Check(sky.CloudOffset() == 2.0f, "the clouds move a unit a tick");
    sky.Tick(4095);
    Check(sky.CloudOffset() == 4097.0f - 4096.0f, "and wrap at 4096");
}

} // namespace

int main() {
    TestRayThroughTheViewCentre();
    TestCloudPlane();
    TestWaterPlane();
    TestEdgeVertex();
    TestStageWithoutCloudsIsFilled();
    TestTickScrollsTheClouds();
    return gFailures == 0 ? 0 : 1;
}
