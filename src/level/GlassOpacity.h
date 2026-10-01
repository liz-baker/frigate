#pragma once

#include <cmath>
#include <cstdint>

#include "level/Matrix.h"

namespace Frigate::Level {

// glassCalculateOpacity: 255 beyond `opadist`, `nearOpacity` (0 to 1) within
// `xludist`, and in between a blend of the two.
inline int32_t GlassOpacity(const Vec3& position, const Vec3& viewer, float xludist, float opadist, float nearOpacity) {
    const float xdiff = position[0] - viewer[0];
    const float ydiff = position[1] - viewer[1];
    const float zdiff = position[2] - viewer[2];
    const float distance = std::sqrt(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

    if (distance > opadist) {
        return 255;
    }
    if (distance < xludist) {
        return static_cast<int32_t>(nearOpacity * 255.0f);
    }
    return static_cast<int32_t>((((distance - xludist) * (1.0f - nearOpacity)) / (opadist - xludist) + nearOpacity) *
                                255.0f);
}

} // namespace Frigate::Level
