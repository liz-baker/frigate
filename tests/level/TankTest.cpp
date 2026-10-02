#include <cmath>
#include <cstdio>

#include "level/Tank.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-3f;
}

void TestModelOffset() {
    TankGeometry geometry;
    geometry.nodes[0] = { 1.0f, 2.0f, 3.0f };
    geometry.nodes[1] = { 0.0f, 0.0f, 10.0f };

    const Vec3 straight = TankModelOffset(geometry, 0.0f);
    Check(Near(straight[0], 1.0f) && Near(straight[1], 2.0f) && Near(straight[2], 13.0f),
          "an unturned turret leaves the seat offset on the pivot");

    const Vec3 turned = TankModelOffset(geometry, 3.14159265f);
    Check(Near(turned[0], 1.0f) && Near(turned[2], -7.0f), "a half-turned turret swings the seat to the other side");
}

void TestCollisionBounds() {
    ModelBounds hull{};
    hull.xmin = -2.0f;
    hull.xmax = 2.0f;
    hull.zmin = -4.0f;
    hull.zmax = 4.0f;

    const auto polygon = TankCollisionBounds(hull, { 0.0f, 0.0f, 0.0f }, 1.0f, { 10.0f, 0.0f, 20.0f }, 0.0f);
    Check(Near(polygon[0][0], 8.0f) && Near(polygon[0][1], 16.0f), "first corner at the hull's low corner");
    Check(Near(polygon[2][0], 12.0f) && Near(polygon[2][1], 24.0f), "third corner at the hull's high corner");
}

} // namespace

int main() {
    TestModelOffset();
    TestCollisionBounds();
    if (gFailures == 0) {
        std::printf("TankTest: all passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
