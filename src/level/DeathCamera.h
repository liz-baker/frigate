#pragma once

#include <cstdint>
#include <functional>

#include "level/Matrix.h"
#include "level/Stan.h"

namespace Frigate::Level {

// What pickDeathCameraAngles reads: the point the camera looks at, where the
// subject stands (`collisionPosition` on `tile`), how far from it the camera
// goes and the radius it keeps clear of walls.
struct DeathCameraRequest {
    Vec3 lookAt{};
    Vec3 collisionPosition{};
    int tile = kNoTile;
    float distance = 0.0f;
    float clearance = 0.0f;
    // sub_GAME_7F03D058 on the subject's prop: takes it out of the line tests
    // (false) and puts it back (true).
    std::function<void(bool collides)> setSubjectCollides;
};

// pickDeathCameraAngles: tries up to 129 random bearings, 16 steps of 22.5
// degrees each, for a spot at least `distance` from the subject with a clear
// line to it. On success `*camera` is the spot; true when one was found.
bool PickDeathCameraAngles(Stan& stan, const DeathCameraRequest& request, const std::function<uint32_t()>& random,
                           Vec3* camera);

} // namespace Frigate::Level
