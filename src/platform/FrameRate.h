#pragma once

#include <algorithm>

namespace Frigate::Platform {

// The frame rate limit from the menu's Settings tab, in frames per second;
// 0 or unset means no limit.
inline constexpr const char* kFpsLimitCVar = "gFpsLimit";

// The game advances one tick per drawn frame, so the limit slows the game
// down rather than only drawing less.
inline int LimitedFps(int wanted, int limit) {
    return limit > 0 ? std::min(wanted, limit) : wanted;
}

} // namespace Frigate::Platform
