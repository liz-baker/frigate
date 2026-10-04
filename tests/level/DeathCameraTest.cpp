#include <cmath>
#include <cstdio>

#include "StanFixture.h"
#include "level/DeathCamera.h"

using namespace Frigate::Level;
using namespace StanFixture;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

struct Lcg {
    uint32_t state = 12345;
    uint32_t operator()() {
        state = (state * 1664525u) + 1013904223u;
        return state;
    }
};

DeathCameraRequest RequestAt(float x, float z, float distance, int* toggles) {
    DeathCameraRequest request;
    request.lookAt = { x, 0.0f, z };
    request.collisionPosition = { x, 0.0f, z };
    request.tile = 0;
    request.distance = distance;
    request.clearance = 30.0f;
    request.setSubjectCollides = [toggles](bool) { (*toggles)++; };
    return request;
}

void TestFindsASpotOnOpenFloor() {
    Stan stan;
    stan.Load(MakeFloor(2000, 0, 0, 1), 1.0f);

    int toggles = 0;
    const DeathCameraRequest request = RequestAt(500.0f, 1000.0f, 200.0f, &toggles);
    Lcg random;
    Vec3 camera{};
    Check(PickDeathCameraAngles(stan, request, std::ref(random), &camera), "a spot is found on open floor");

    const float dx = camera[0] - 500.0f;
    const float dz = camera[2] - 1000.0f;
    const float distance = std::sqrt((dx * dx) + (dz * dz));
    Check(distance >= 200.0f - 0.01f, "the camera keeps its distance from the subject");
    Check(camera[0] > 0.0f && camera[0] < 2000.0f && camera[2] > 0.0f && camera[2] < 2000.0f,
          "the camera stands on the floor");
    Check(camera[1] >= 30.0f && camera[1] <= 185.0f, "the camera hangs between the clearance and 185 above the floor");
    Check(toggles > 0 && toggles % 2 == 0, "the subject is taken out of the line tests and put back each time");
}

void TestGivesUpWhenThereIsNoRoom() {
    Stan stan;
    stan.Load(MakeFloor(100, 0, 0, 1), 1.0f);

    int toggles = 0;
    const DeathCameraRequest request = RequestAt(30.0f, 50.0f, 500.0f, &toggles);
    Lcg random;
    Vec3 camera{};
    Check(!PickDeathCameraAngles(stan, request, std::ref(random), &camera),
          "no spot fits a floor smaller than the distance");
}

} // namespace

int main() {
    TestFindsASpotOnOpenFloor();
    TestGivesUpWhenThereIsNoRoom();
    if (gFailures == 0) {
        std::printf("all death camera tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
