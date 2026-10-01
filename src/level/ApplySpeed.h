#pragma once

#include <cstdint>

namespace Frigate::Level {

// chrobjApplySpeed: eases `*position` toward `target` over `ticks` clock
// ticks, accelerating by `accel` up to `maxSpeed` and slowing by `decel` to
// stop on the target. `*speed` carries the speed between calls.
void ChrObjApplySpeed(float* position, float target, float* speed, float accel, float decel, float maxSpeed,
                      int32_t ticks);

// chrobjCallsApplySpeed: the same for an angle in radians, which takes the
// short way round to `target` and stays within [0, 2*pi).
void ChrObjCallsApplySpeed(float* position, float target, float* speed, float accel, float decel, float maxSpeed,
                           int32_t ticks);

} // namespace Frigate::Level
