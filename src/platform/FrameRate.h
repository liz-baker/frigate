#pragma once

#include <algorithm>

namespace Frigate::Platform {

// The frame rate limit from the menu's Settings tab, in frames per second;
// 0 or unset means no limit.
inline constexpr const char* kFpsLimitCVar = "gFpsLimit";

// A stage played live steps as many ticks as its last frame took (game/FrameTiming.h), so the
// limit drops frames and the game keeps its speed; a menu screen advances a fixed number of ticks
// a frame and runs slower under it.
inline int LimitedFps(int wanted, int limit) {
    return limit > 0 ? std::min(wanted, limit) : wanted;
}

} // namespace Frigate::Platform
