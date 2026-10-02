#pragma once

#include <array>
#include <cstdint>

#include "level/Matrix.h"

namespace Frigate::Level {

// What a CCTV camera keeps past the ObjectRecord (CCTVRecord): the matrix that
// turns its model towards the pad it first looks at, and the swing that sweeps
// it between two angles.
struct CctvState {
    // unk84: faces the look pad, with the model's scale.
    Mtxf basis{};
    // unkC4: the heading of the look pad from the camera's pivot.
    float lookHeading = 0.0f;
    // unkC8: how far round the camera has swung.
    float swing = 0.0f;
    // unkCC and unkD0: the two angles it swings between.
    float swingA = 0.0f;
    float swingB = 0.0f;
    // unkD4: swinging back towards `swingA`.
    bool towardsA = false;
    // unkD8 and unkDC: the swing's speed, and the fastest it goes.
    float speed = 0.0f;
    float maxSpeed = 0.0f;
    // timer: ticks the player has been in sight.
    int32_t timer = 0;
    // unkE8: how far it sees; no limit when not above zero.
    float range = 0.0f;
};

// setupCctv: the camera for the object placed at `position` under `matrix`
// (which carries the model's scale), pivoting on `pivot` in the model, looking
// at `lookPad`. `fixed` holds the file's swing angles, speed and range
// (CCTVRecord's unkCC, unkD0, unkDC and unkE8).
CctvState MakeCctv(const Mtxf& matrix, const Vec3& position, const std::array<float, 3>& pivot, const Vec3& lookPad,
                   float modelScale, const std::array<int32_t, 4>& fixed);

// Whether the camera sees a player `toPlayer` away from it (the range and the
// view cone of objTick's CCTV branch; the line of sight is tested apart).
bool CctvInView(const CctvState& state, const Vec3& toPlayer, bool inMotion);

// The swing of objTick's CCTV branch, moved on by `timerDelta` ticks.
void SwingCctv(CctvState* state, float timerDelta);

// The matrix of the swivelling node (slot 1) of a camera whose object matrix
// is `objectMatrix` and whose position is `relativePosition` from what the
// props are drawn about, before the view is applied.
Mtxf CctvSwivel(const CctvState& state, const Mtxf& objectMatrix, const Vec3& relativePosition,
                const std::array<float, 3>& pivot);

} // namespace Frigate::Level
