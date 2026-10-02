#include "level/Beams.h"

#include <cmath>

#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

constexpr float kRandomFraction = 1.0f / 4294967295.0f;

float RandomUnit(const BeamRandomFn& random) {
    return static_cast<float>(random()) * kRandomFraction;
}

} // namespace

void StartBeam(BeamRecord* beam, int32_t item, const Vec3& from, const Vec3& to, const BeamRandomFn& random) {
    beam->position = from;
    beam->delta = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };

    float length = std::sqrt((beam->delta[0] * beam->delta[0]) + (beam->delta[1] * beam->delta[1]) +
                             (beam->delta[2] * beam->delta[2]));
    beam->delta[0] *= 1.0f / length;
    beam->delta[1] *= 1.0f / length;
    beam->delta[2] *= 1.0f / length;

    // The watch laser has a short beam, in keeping with its range; the others
    // are capped so a shot into the void does not glitch.
    if (item == kItemWatchLaser) {
        if (length > 300.0f) {
            length = 300.0f;
        }
    } else if (length > 10000.0f) {
        length = 10000.0f;
    }

    beam->state = 0;
    beam->item = static_cast<int8_t>(item);
    beam->length = length;

    if (length < 500.0f) {
        length = 500.0f;
    }

    if (item == kItemLaser) {
        beam->streak = 0.25f * length;
        beam->drawLength = 0.6f * length;
        if (beam->drawLength > 3000.0f) {
            beam->drawLength = 3000.0f;
        }
        // Laser beams show more often than other weapons'.
        beam->tail = (-0.1f - (RandomUnit(random) * 0.3f)) * length;
    } else if (item == kItemWatchLaser) {
        beam->drawLength = length;
        beam->streak = 2.0f * length;
        if (length > 3000.0f) {
            beam->drawLength = 3000.0f;
        }
        // The watch laser's beam always shows.
        beam->tail = 0.0f;
    } else {
        beam->streak = 0.2f * length;
        beam->drawLength = beam->streak;
        if (beam->streak > 3000.0f) {
            beam->drawLength = 3000.0f;
        }
        // Whether a normal bullet's beam shows.
        beam->tail = ((2.0f * RandomUnit(random)) - 1.0f) * beam->streak;
    }

    if (beam->length <= beam->tail) {
        beam->state = -1;
    }
}

void AdvanceBeam(BeamRecord* beam, int32_t clockTimer, float globalTimerDelta, const BeamRandomFn& random) {
    if (beam->state < 0) {
        return;
    }

    if (clockTimer < 3) {
        beam->tail += beam->streak * globalTimerDelta;
    } else {
        beam->tail += beam->streak * (2.0f + (static_cast<float>(random()) * 2.3283064e-10f * 0.5f));
    }

    if (beam->length <= beam->tail) {
        beam->state = -1;
        return;
    }
    beam->state++;
}

} // namespace Frigate::Level
