#include <cmath>
#include <cstdio>

#include <algorithm>

#include "gfx/Gu.h"
#include "level/Fog.h"
#include "level/PropFog.h"

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

// Dam's near fog: 3333 to 4444, from 600.
Fog DamFog() {
    Fog fog;
    fog.LoadLevelEnvironment(LevelId::Dam, false, 1, 0.2f);
    return fog;
}

void TestFogVisibility() {
    const Fog fog = DamFog();
    Check(FogVisibility(fog, 500.0f, 100.0f, 1.0f) == 1.0f, "a prop inside the obscuring range is clear");
    Check(FogVisibility(fog, 2000.0f, 100.0f, 1.0f) == 1.0f, "and so is one before the near fog");
    Check(Near(FogVisibility(fog, 4000.0f, 100.0f, 1.0f), 0.3996f), "one in it fades");
    Check(FogVisibility(fog, 5000.0f, 100.0f, 1.0f) == 0.0f, "one beyond it is gone");
    // A smaller prop is taken to be further away.
    Check(Near(FogVisibility(fog, 2000.0f, 50.0f, 1.0f), 0.9397f), "a small prop fades sooner");
    // A narrower field of view stretches distances.
    Check(FogVisibility(fog, 3000.0f, 100.0f, 2.0f) == 0.0f, "as does a zoomed view");

    Fog none;
    none.LoadLevelEnvironment(LevelId::Bunker1, false, 1, 1.0f);
    Check(FogVisibility(none, 1e6f, 1.0f, 1.0f) == 1.0f, "a stage without near fog hides nothing");
}

void TestNearFogVisible() {
    const Fog fog = DamFog();
    Frigate::Gu::MatrixF identity = {};
    for (size_t i = 0; i < 4; i++) {
        identity[i][i] = 1.0f;
    }
    const Vec3 viewer = { 0.0f, 0.0f, 0.0f };
    Check(NearFogVisible(fog, { 1000.0f, 0.0f, 0.0f }, 100.0f, viewer, identity, 1.0f), "a prop well inside");
    Check(!NearFogVisible(fog, { 5000.0f, 0.0f, 0.0f }, 100.0f, viewer, identity, 1.0f),
          "and one past the visible range");
    Check(NearFogVisible(fog, { 500.0f, 0.0f, 0.0f }, 100.0f, viewer, identity, 1.0f),
          "one inside the obscuring range");
}

void TestLerpTowardsFog() {
    IntColour colour = { 100, 100, 100, 100 };
    LerpTowardsFog(colour, Fog::kPropFogged, { 1.0f, 0.0f, 0.0f, 0.5f });
    Check(colour[0] == 177 && colour[1] == 50 && colour[2] == 50 && colour[3] == 177, "half way to the fog colour");

    IntColour untouched = { 100, 100, 100, 100 };
    LerpTowardsFog(untouched, Fog::kPropClear, { 1.0f, 0.0f, 0.0f, 0.5f });
    Check(untouched[0] == 100 && untouched[3] == 100, "a prop the fog does not reach is not");
}

void TestPackColour() {
    Check(PackColour({ 0x12, 0x34, 0x56, 0x78 }) == 0x12345678u, "red in the top byte");
}

} // namespace

int main() {
    TestFogVisibility();
    TestNearFogVisible();
    TestLerpTowardsFog();
    TestPackColour();
    return gFailures == 0 ? 0 : 1;
}
