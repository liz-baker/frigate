#pragma once

#include <cstdint>

namespace Frigate::Game::Menu {

// joyGetStickXInRange / joyGetStickYInRange(player, -2, 1): a stick axis as
// one of -2 to 1. A centred stick reads -1; 1 is 60 and over towards positive
// and -2 is -21 and under.
inline int StickInRange(int8_t axis) {
    // JOY_CLAMP_OFFSET and JOY_CLAMP_MAX.
    constexpr int kOffset = 60;
    constexpr int kMax = 120;

    int shifted = axis + kOffset;
    if (shifted > kMax) {
        shifted = kMax;
    }
    if (shifted < 0) {
        shifted = 0;
    }
    return ((shifted * 3) / kMax) - 2;
}

} // namespace Frigate::Game::Menu
