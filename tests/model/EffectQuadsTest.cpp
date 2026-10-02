#include "model/EffectQuads.h"

#include <cstdio>
#include <cstdlib>

#include "model/Random.h"
#include "model/Trig.h"

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Mat4 Identity() {
    Mat4 mtx{};
    for (int i = 0; i < 4; i++) {
        mtx.m[i][i] = 1.0f;
    }
    return mtx;
}

void TestRandomMatchesTheGamesGenerator() {
    // The first outputs from the initial state and the state after a seed of 5,
    // worked out by hand from the generator's shifts and xors.
    Check(RandomGetNext() == 0x40ec37cf, "first output");
    Check(RandomGetNext() == 0x630aedd7, "second output");
    Check(RandomGetNext() == 0x1f58071e, "third output");
    RandomSetSeed(5);
    Check(RandomGetNext() == 0x6003, "seeding sets the state to the seed plus one");
}

void TestTrig() {
    Check(Sins(0) == 0 && Sins(0x4000) == 32767 && Sins(0xC000) == -32767, "sins at the quarter turns");
    Check(Coss(0) == 32767 && Coss(0x8000) == -32767 && Coss(0x4000) == 0, "coss at the quarter turns");
}

void TestShadowAlpha() {
    Check(ShadowAlpha(false, 0x12345678, 80) == 80, "a plain prop's shadow takes the level");
    Check(ShadowAlpha(true, 0xFF, 80) == 80, "a character in full light keeps the level");
    Check(ShadowAlpha(true, 0x80, 80) == 40, "the environment's low byte scales a character's");
}

void TestShadowQuad() {
    const float position[2] = { 10.0f, 20.0f };
    const float size[2] = { 100.0f, 40.0f };

    // On the ground: 1.25 times as big, at the ground (or just above it with
    // the z-buffer on), each vertex carrying the alpha.
    EffectQuad flat = BuildShadowQuad(position, size, 0.0f, 0.5f, false, 80, nullptr);
    Check(flat[0].position[0] == -115 && flat[0].position[2] == -30, "first corner");
    Check(flat[2].position[0] == 135 && flat[2].position[2] == 70, "opposite corner");
    Check(flat[0].position[1] == 0 && flat[3].colour[3] == 80, "on the ground, with the alpha");
    Check(BuildShadowQuad(position, size, 0.0f, 0.5f, true, 80, nullptr)[0].position[1] == 4,
          "just above the ground with the z-buffer");

    // Shrinks with height, and is gone above 300.
    EffectQuad high = BuildShadowQuad(position, size, 100.0f, 1.0f, false, 80, nullptr);
    Check(high[0].position[0] == -90 && high[0].position[1] == -100, "at 100 units the shadow is its own size");
    EffectQuad gone = BuildShadowQuad(position, size, 400.0f, 1.0f, false, 80, nullptr);
    Check(gone[0].position[0] == 10 && gone[2].position[0] == 10 && gone[0].position[2] == 20, "no size above 300");

    const EffectImage image{ 32, 16 };
    EffectQuad textured = BuildShadowQuad(position, size, 0.0f, 1.0f, false, 80, &image);
    Check(textured[0].s == 0 && textured[0].t == 0 && textured[1].s == 1023 && textured[2].t == 511 &&
              textured[3].s == 0 && textured[3].t == 511,
          "the image's corners");
}

void TestGunfireQuad() {
    // Looking straight down z with nothing to face towards, the flash is a
    // rectangle in the x-y plane. With a random number of 0 it is at 0.75 of
    // its size, and it runs from its offset back by that width.
    const float offset[3] = { 0.0f, 0.0f, 0.0f };
    const float size[3] = { 100.0f, 40.0f, 100.0f };
    const auto zero = []() -> uint32_t { return 0; };
    EffectQuad quad = BuildGunfireQuad(Identity(), offset, size, 1.0f, nullptr, zero);
    Check(quad[0].position[0] == 0 && quad[0].position[1] == -15 && quad[1].position[1] == 15, "height");
    // The game's table-driven acosf leaves the facing angles a hair off, which
    // the integer positions can show as a unit.
    Check(std::abs(quad[2].position[0] + 75) <= 1 && std::abs(quad[3].position[0] + 75) <= 1, "width");
    Check(quad[0].position[2] == 0 && quad[2].position[2] == 0, "flat");
    Check(quad[0].colour[0] == 0xFF && quad[0].colour[3] == 0xFF, "opaque white");

    // A textured flash turns its coordinates about the image's centre by an
    // angle from the second random number.
    const EffectImage image{ 32, 32 };
    EffectQuad textured = BuildGunfireQuad(Identity(), offset, size, 1.0f, &image, zero);
    Check(textured[0].s == -211 && textured[0].t == 512, "first corner's coordinates");
    Check(textured[1].s == 512 && textured[1].t == -211, "second corner's coordinates");
    Check(textured[2].s == 1235 && textured[3].t == 1235, "opposite corners' coordinates");

    // Without a source given, the game's generator is used.
    RandomSetSeed(1234);
    EffectQuad random = BuildGunfireQuad(Identity(), offset, size, 1.0f, nullptr);
    Check(random[2].position[0] < -60 && random[2].position[0] > -130, "sized between 0.75 and 1.25 of the size");
}

} // namespace

int main() {
    TestRandomMatchesTheGamesGenerator();
    TestTrig();
    TestShadowAlpha();
    TestShadowQuad();
    TestGunfireQuad();
    return gFailures == 0 ? 0 : 1;
}
