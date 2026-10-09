#include <cmath>
#include <cstdio>

#include "level/VehicleWheels.h"

using namespace Frigate::Level;

namespace {

constexpr float kTau = 6.2831855f;

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

// A wheel 40 units tall and a 200 unit wheelbase.
VehicleGeometry Truck() {
    VehicleGeometry geometry;
    geometry.points[0] = { -50.0f, 20.0f, 100.0f };
    geometry.points[1] = { 50.0f, 20.0f, 100.0f };
    geometry.points[2] = { -50.0f, 20.0f, -100.0f };
    geometry.points[3] = { 50.0f, 20.0f, -100.0f };
    geometry.hull = { -60.0f, 60.0f, 0.0f, 40.0f, -120.0f, 120.0f };
    return geometry;
}

void TestSpinIsDoubledForTheOwner() {
    ObjectAi ai;
    ai.speed = 4.0f;
    // distance / (size / 2) = 4 * 2 / 20 per application.
    StepVehicleWheels(ai, Truck(), 1.0f, 2.0f, true);
    Check(Near(ai.wheelXRot, 0.8f), "the owner's wheels turn twice the distance over the radius");

    ObjectAi other;
    other.speed = 4.0f;
    StepVehicleWheels(other, Truck(), 1.0f, 2.0f, false);
    Check(Near(other.wheelXRot, 0.0f), "a non-owner does not spin the wheels");
}

void TestSpinScalesWithTheModel() {
    ObjectAi ai;
    ai.speed = 4.0f;
    StepVehicleWheels(ai, Truck(), 2.0f, 2.0f, true);
    Check(Near(ai.wheelXRot, 0.4f), "a larger model's wheels turn slower");
}

void TestSpinWraps() {
    ObjectAi ai;
    ai.speed = 4.0f;
    ai.wheelXRot = kTau - 0.1f;
    StepVehicleWheels(ai, Truck(), 1.0f, 2.0f, true);
    Check(ai.wheelXRot >= 0.0f && ai.wheelXRot < kTau, "the roll stays in [0, tau)");
    Check(Near(ai.wheelXRot, 0.7f), "the roll wraps round");

    ObjectAi reverse;
    reverse.speed = -4.0f;
    StepVehicleWheels(reverse, Truck(), 1.0f, 2.0f, true);
    Check(reverse.wheelXRot >= 0.0f && reverse.wheelXRot < kTau, "a negative roll wraps up");
}

void TestSteering() {
    ObjectAi straight;
    straight.speed = 2.0f;
    StepVehicleWheels(straight, Truck(), 1.0f, 1.0f, true);
    Check(Near(straight.wheelYRot, 0.0f), "the front wheels point ahead when not turning");

    ObjectAi left;
    left.speed = 2.0f;
    left.turnRate = -0.1f;
    StepVehicleWheels(left, Truck(), 1.0f, 1.0f, true);
    Check(left.wheelYRot >= 0.1f && left.wheelYRot < kTau / 2,
          "a negative turn steers by an angle no less than the turn");

    ObjectAi right;
    right.speed = 2.0f;
    right.turnRate = 0.1f;
    StepVehicleWheels(right, Truck(), 1.0f, 1.0f, true);
    Check(Near(right.wheelYRot, kTau - left.wheelYRot), "a positive turn steers the other way");

    ObjectAi stopped;
    stopped.wheelYRot = 0.5f;
    stopped.turnRate = 0.3f;
    StepVehicleWheels(stopped, Truck(), 1.0f, 1.0f, true);
    Check(Near(stopped.wheelYRot, 0.5f), "a stopped vehicle keeps its wheels where they were");
}

void TestPose() {
    ObjectAi ai;
    ai.wheelXRot = 1.0f;
    ai.wheelYRot = 0.5f;
    const VehicleGeometry geometry = Truck();
    const std::array<Mtxf, 5> pose = VehicleWheelPose(geometry, ai, IdentityMtxf());
    for (size_t i = 0; i < 4; i++) {
        Check(Near(pose[i + 1].m[3][0], geometry.points[i][0]) && Near(pose[i + 1].m[3][1], geometry.points[i][1]) &&
                  Near(pose[i + 1].m[3][2], geometry.points[i][2]),
              "each wheel sits on its node");
    }
    Check(Near(pose[3].m[0][0], 1.0f) && Near(pose[3].m[1][1], std::cos(1.0f)), "a rear wheel only rolls about X");
    Check(!Near(pose[1].m[0][0], 1.0f), "a front wheel is turned about Y as well");
}

} // namespace

int main() {
    TestSpinIsDoubledForTheOwner();
    TestSpinScalesWithTheModel();
    TestSpinWraps();
    TestSteering();
    TestPose();
    return gFailures == 0 ? 0 : 1;
}
