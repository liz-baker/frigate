#include "model/GameMath.h"

#include <cmath>
#include <cstdio>

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Same(float a, float b) {
    return std::fabs(a - b) <= std::fabs(b) * 1e-7f;
}

void TestAtan2ReturnsAnAngleInAFullTurn() {
    Check(Same(GameAtan2f(1.0f, 1.0f), 0.785338283f), "a bearing in the first quadrant");
    Check(Same(GameAtan2f(-3662.0f, -583.0f), 4.55419493f), "a bearing past a half turn is positive");
    Check(Same(GameAtan2f(5.0f, -2.0f), 1.95125341f), "the second quadrant");
    Check(Same(GameAtan2f(-7.0f, 3.0f), 5.11717463f), "the fourth quadrant");
    Check(Same(GameAtan2f(-0.5f, 100.0f), 6.27827168f), "just below a full turn");
}

void TestAtan2OnTheAxes() {
    Check(GameAtan2f(0.0f, 4.0f) == 0.0f, "along +x");
    Check(Same(GameAtan2f(0.0f, -4.0f), 3.14159274f), "along -x is a half turn");
    Check(Same(GameAtan2f(2.0f, 0.0f), 1.57079637f), "along +y is a quarter turn");
    Check(Same(GameAtan2f(-2.0f, 0.0f), 4.71238899f), "along -y is three quarters");
}

void TestArcFunctionsFollowTheTable() {
    Check(Same(GameAcosf(0.5f), 1.0472455f), "acos of a half");
    Check(Same(GameAcosf(-0.5f), 2.09434724f), "acos of minus a half");
    Check(Same(GameAcosf(0.999f), 0.0453490019f), "acos near one");
    Check(Same(GameAcosf(1.0f), 0.00278038252f), "acos of one stops short of zero");
    Check(Same(GameAcosf(-1.0f), 3.1388123f), "acos of minus one stops short of a half turn");
    Check(Same(GameAsinf(0.5f), 0.523526907f), "asin of a half");
    Check(Same(GameAsinf(-0.5f), -0.523574829f), "asin of minus a half");
    Check(Same(GameAsinf(0.123f), 0.123247653f), "asin of a small value");
}

void TestSineTracksTheRealSine() {
    for (int step = -40; step <= 40; step++) {
        const float angle = static_cast<float>(step) * 0.37f;
        Check(std::fabs(GameSinf(angle) - std::sin(angle)) < 2e-6f, "the sine agrees with the library's");
        Check(std::fabs(GameCosf(angle) - std::cos(angle)) < 2e-6f, "the cosine agrees with the library's");
    }
}

void TestSineSpecialRanges() {
    Check(GameSinf(1e-5f) == 1e-5f, "a tiny angle is its own sine");
    Check(GameSinf(-1e-5f) == -1e-5f, "a tiny negative angle too");
    Check(GameSinf(3.0e9f) == 0.0f, "a huge angle gives zero");
    Check(GameSinf(-3.0e9f) == 0.0f, "a huge negative angle gives zero");
    Check(GameSinf(0.0f) == 0.0f, "sine of zero");
    Check(std::fabs(GameCosf(0.0f) - 1.0f) < 1e-6f, "cosine of zero");
}

} // namespace

int main() {
    TestAtan2ReturnsAnAngleInAFullTurn();
    TestAtan2OnTheAxes();
    TestArcFunctionsFollowTheTable();
    TestSineTracksTheRealSine();
    TestSineSpecialRanges();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failures\n", gFailures);
        return 1;
    }
    std::printf("GameMath tests passed\n");
    return 0;
}
