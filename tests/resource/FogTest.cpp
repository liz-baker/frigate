#include <cmath>
#include <cstdio>

#include <algorithm>
#include <array>

#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "level/Fog.h"

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

std::array<std::array<float, 4>, 4> Identity() {
    std::array<std::array<float, 4>, 4> m{};
    for (size_t i = 0; i < 4; i++) {
        m[i][i] = 1.0f;
    }
    return m;
}

void TestSinglePlayerStageTakesItsRecord() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Dam, false, 1, 0.2f);
    Check(fog.Enabled(), "Dam has fog");
    Check(fog.ZNear() == 5.0f && fog.ZFar() == 15000.0f, "the record's z range is the projection's");
    const CurrentEnvironment& env = fog.Environment();
    Check(env.differenceFromFarIntensity == 995 && env.farIntensity == 1000, "the fog runs 995 to 1000 thousandths");
    Check(env.sky.red == 0x10 && env.sky.green == 0x30 && env.sky.blue == 0x60, "the fog and sky colour");
    Check(env.sky.clouds == 1 && env.sky.cloudRepeat == 5000.0f, "the cloud plane");
    const NearFog* nearFog = fog.NearFogValues();
    Check(nearFog != nullptr && nearFog->nearFog == 3333.0f && nearFog->maxVisRange == 4444.0f &&
              nearFog->maxObfuscationRange == 600.0f,
          "the near fog");
}

void TestScaledFogDistances() {
    // Independent of the code under test: the decomp's arithmetic worked
    // through in single precision for Dam's record at its 0.2 visibility scale.
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Dam, false, 1, 0.2f);
    Check(Near(fog.ScaledFarFogIntensity(), 75000.0f), "the far fog distance is the z range's, unscaled");

    FogColour colour{};
    Check(fog.PropDistColour(100.0f, &colour) == Fog::kPropClear, "a near prop is clear of the fog");
    Check(fog.PropDistColour(-5.0f, &colour) == Fog::kPropClear, "a prop behind the camera is clear");
    Check(fog.PropDistColour(10000.0f, &colour) == Fog::kPropFogged, "a distant prop is fogged");
    Check(Near(colour[3], 0.77762f, 1e-3f), "by the fraction the fog covers it");
    Check(Near(colour[0], 16.0f / 255.0f) && Near(colour[2], 96.0f / 255.0f), "towards the fog colour");
    Check(fog.PropDistColour(80000.0f, &colour) == Fog::kPropHidden, "a prop lost in the fog is hidden");
}

void TestFoglessStage() {
    Fog fog;
    // Bunker 1 has records for multiplayer only.
    fog.LoadLevelEnvironment(LevelId::Bunker1, false, 1, 1.0f);
    Check(!fog.Enabled(), "Bunker 1 has no fog");
    Check(fog.ZNear() == 15.0f && fog.ZFar() == 10000.0f, "the default z range");
    Check(fog.Environment().sky.green == 0x10 && fog.Environment().sky.blue == 0x40 &&
              fog.Environment().sky.clouds == 0,
          "and the default sky, a plain colour");
    FogColour colour{};
    Check(fog.PropDistColour(50000.0f, &colour) == Fog::kPropClear, "nothing hides in a stage with no fog");
    Check(fog.ScaledFarFogIntensity() > 1e30f, "and nothing is beyond it");

    Gfx buffer[4] = {};
    Check(fog.SetRenderFogColor(buffer) == buffer && fog.ClearFogMode(buffer) == buffer,
          "the fog commands draw nothing");

    fog.LoadLevelEnvironment(LevelId::Frigate, false, 1, 1.0f);
    Check(fog.Environment().sky.isWater == 1 && fog.Environment().sky.waterImageId == 2 &&
              fog.Environment().sky.cloudRepeat == 3000.0f,
          "Frigate's fogless sky has water");
    fog.LoadLevelEnvironment(LevelId::Cuba, false, 1, 1.0f);
    Check(fog.Environment().sky.red == 0x30 && fog.Environment().sky.isWater == 0, "and Cuba's its own colour");
}

void TestVariants() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Dam, true, 1, 0.2f);
    Check(fog.ZNear() == 30.0f, "the cinema record blends further");

    fog.LoadLevelEnvironment(LevelId::Bunker1, false, 2, 1.0f);
    Check(fog.Enabled() && fog.ZFar() == 2500.0f, "Bunker 1 for two players has its own record");
    fog.LoadLevelEnvironment(LevelId::Jungle, false, 2, 1.0f);
    Check(fog.Enabled() && fog.ZFar() == 7500.0f, "a stage with none takes the two player default");
    fog.LoadLevelEnvironment(LevelId::Jungle, false, 1, 1.0f);
    Check(fog.Enabled() && fog.ZFar() == 2500.0f, "one player takes the stage's own");
}

void TestSwitchToSolosky2() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Facility, false, 1, 1.0f);
    Check(fog.ZFar() == 5000.0f && fog.Environment().sky.red == 0x10, "the main record");
    fog.SwitchToSolosky2(1.0f);
    Check(fog.ZFar() == 1000.0f, "the alternate record when fully switched");
    Check(fog.Environment().sky.red == 0x40 && fog.Environment().sky.green == 0x80, "with its colours");
    fog.SwitchToSolosky2(0.5f);
    Check(fog.ZFar() == 3000.0f, "half way between");
    Check(fog.Environment().sky.red == 0x28 && (fog.Environment().sky.red & 0x07) == 0,
          "colours keep their top five bits");
    fog.SwitchToSolosky2(0.0f);
    Check(fog.ZFar() == 5000.0f, "and back to the main");
}

void TestFogCommands() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Dam, false, 1, 0.2f);
    Gfx buffer[8] = {};
    Gfx* end = fog.SetRenderFogColor(buffer);
    Check(end - buffer == 4, "four commands set the fog");
    Check((buffer[0].words.w0 >> 24) == 0xF8 && buffer[0].words.w1 == 0x103060FF, "the fog colour, opaque");
    // gSPFogPosition(995, 1000): a multiplier of 128000 / 5 and an offset of
    // (500 - 995) * 256 / 5, in the halves of a G_MW_FOG move-word.
    Check(buffer[1].words.w0 == ((static_cast<uint32_t>(static_cast<uint8_t>(Fast::F3DEX_G_MOVEWORD)) << 24) | 0x08) &&
              buffer[1].words.w1 == 0x64009D00,
          "the fog position");
    Check(buffer[2].words.w1 == G_FOG, "the geometry mode that applies it");
    Check((buffer[3].words.w0 & 0xFFFF) == ((G_MDSFT_ALPHADITHER << 8) | 2) && buffer[3].words.w1 == G_AD_NOISE,
          "and noise dithering");

    Gfx* cleared = fog.ClearFogMode(buffer);
    Check(cleared - buffer == 1 && buffer[0].words.w1 == G_FOG, "clearing the geometry mode again");
}

void TestPositionVisibleThroughFog() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Dam, false, 1, 0.2f);
    const auto view = Identity();
    Frigate::Gu::MatrixF matrix;
    for (size_t i = 0; i < 4; i++) {
        for (size_t j = 0; j < 4; j++) {
            matrix[i][j] = view[i][j];
        }
    }
    const Vec3 viewer = { 0.0f, 0.0f, 0.0f };
    Check(fog.PositionIsVisibleThroughFog({ 74000.0f, 0.0f, 0.0f }, 0.0f, viewer, matrix), "inside the far fog");
    Check(!fog.PositionIsVisibleThroughFog({ 76000.0f, 0.0f, 0.0f }, 0.0f, viewer, matrix), "beyond it");
    Check(fog.PositionIsVisibleThroughFog({ 76000.0f, 0.0f, 0.0f }, 2000.0f, viewer, matrix),
          "but a large enough prop is still seen");

    Fog none;
    none.LoadLevelEnvironment(LevelId::Bunker1, false, 1, 1.0f);
    Check(none.PositionIsVisibleThroughFog({ 1e9f, 0.0f, 0.0f }, 0.0f, viewer, matrix),
          "a stage with no fog hides nothing");
}

} // namespace

int main() {
    TestSinglePlayerStageTakesItsRecord();
    TestScaledFogDistances();
    TestFoglessStage();
    TestVariants();
    TestSwitchToSolosky2();
    TestFogCommands();
    TestPositionVisibleThroughFog();
    return gFailures == 0 ? 0 : 1;
}
