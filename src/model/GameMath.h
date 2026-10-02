#pragma once

namespace Frigate {

// The game's own acosf, asinf and atan2f. They interpolate a 16-bit table of
// the arccosine instead of calling the C library, and atan2f returns an angle
// in [0, 2pi) rather than (-pi, pi]; the guards' bearings and headings (sight
// cones, turning, explosion directions) depend on that range.
float GameAcosf(float value);
float GameAsinf(float value);
float GameAtan2f(float y, float x);

} // namespace Frigate
