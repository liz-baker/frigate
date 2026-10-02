#include <cmath>
#include <cstdio>

#include "level/Autogun.h"
#include "level/Rotor.h"

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

SwitchGeometry MakeGeometry() {
    SwitchGeometry geometry;
    geometry.origin[1] = { 1.0f, 2.0f, 3.0f };
    geometry.origin[2] = { 0.0f, 10.0f, 0.0f };
    geometry.origin[3] = { 0.0f, 0.0f, 5.0f };
    geometry.origin[5] = { 0.0f, 0.0f, 20.0f };
    geometry.matrix[1] = 1;
    geometry.matrix[2] = 2;
    geometry.matrix[3] = 3;
    geometry.matrix[5] = 3;
    return geometry;
}

void TestSetup() {
    const Vec3 position = { 0.0f, 0.0f, 0.0f };
    const Vec3 pad = { 100.0f, 0.0f, 100.0f };
    // Limits of a quarter turn either way, a turn speed, and a range of 100 units.
    const AutogunState gun = MakeAutogun(position, &pad, { 16384, -16384, 1000, 65536 });
    Check(Near(gun.limitMax, kTau / 4.0f) && Near(gun.limitMin, -kTau / 4.0f),
          "the limits are converted from the file's");
    Check(Near(gun.speed, 1000.0f * kTau / 65536.0f), "and the speed");
    Check(Near(gun.aimDistance, 100.0f), "and the range, in units of a hundred");
    Check(Near(gun.restYaw, 3.14159265f / 4.0f), "a gun with a pad rests pointing at it");
    Check(Near(gun.restPitch, 0.0f), "level with it");
    Check(!gun.active && gun.lastSighted == -1 && gun.firstSighted == -1, "and has not seen anything");

    const AutogunState padless = MakeAutogun(position, nullptr, { 0, 0, 0, 0 });
    Check(padless.restYaw == 0.0f && padless.restPitch == 0.0f, "a gun with no pad rests straight ahead");
}

void TestPose() {
    const SwitchGeometry geometry = MakeGeometry();
    AutogunState gun;
    gun.yaw = 0.0f;
    gun.pitch = 0.0f;
    Mtxf body = IdentityMtxf();
    body.m[3][0] = 100.0f;
    const AutogunPose pose = PoseAutogun(geometry, gun, body, 1.0f);
    Check(pose.posed[1] && pose.posed[2] && pose.posed[3] && !pose.posed[4], "the nodes the model has are posed");
    // The heading pivot sits where the body puts its origin.
    Check(Near(pose.matrix[1].m[3][0], 101.0f) && Near(pose.matrix[1].m[3][1], 2.0f) &&
              Near(pose.matrix[1].m[3][2], 3.0f),
          "the heading turns about the body's pivot");
    // A heading of zero turns a quarter turn (the model faces along x).
    Check(Near(pose.matrix[1].m[0][2], -1.0f) && Near(pose.matrix[1].m[2][0], 1.0f),
          "a gun pointing along z has its heading node turned a quarter");
    Check(Near(pose.matrix[2].m[3][1], 12.0f), "the pitch node is carried by it");

    const Mtxf muzzle = AutogunNodeMatrix(geometry, pose, body, 5);
    Check(Near(muzzle.m[3][0], pose.matrix[3].m[3][0]) && Near(muzzle.m[3][2], pose.matrix[3].m[3][2]),
          "a muzzle flash is carried by the barrel it sits under");

    const AutogunPose small = PoseAutogun(geometry, gun, body, 0.5f);
    Check(Near(small.matrix[1].m[2][0], 0.5f), "the model's scale applies to the heading node");
}

void TestRotor() {
    const SwitchGeometry geometry = MakeGeometry();
    const Mtxf body = IdentityMtxf();
    const RotorPose rotor = PoseRotor(geometry, 0.0f, false, body);
    Check(rotor.posed[2] && rotor.posed[3] && !rotor.posed[4], "the rotors the model has are posed");
    Check(Near(rotor.matrix[2].m[3][1], 10.0f), "the main rotor sits on its pivot");
    const RotorPose turned = PoseRotor(geometry, kTau / 4.0f, false, body);
    Check(Near(turned.matrix[2].m[0][2], -1.0f), "the main rotor turns about y");
    const RotorPose moving = PoseRotor(geometry, kTau / 4.0f, true, body);
    Check(Near(moving.matrix[2].m[0][1], 1.0f), "and about z for an aircraft in motion");
}

} // namespace

int main() {
    TestSetup();
    TestPose();
    TestRotor();
    if (gFailures == 0) {
        std::printf("AutogunTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
