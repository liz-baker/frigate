#pragma once

#include <cstdint>

#include "game/FrontState.h"

namespace Frigate::Game::Menu {

// The side tabs of the mode, mission and difficulty screens (front.h): the
// edges the cursor is tested against and the text's extent on each tab.
constexpr float kTabsLeftEdge = 390.0f;
constexpr int32_t kTabsRightEdge = 411;

constexpr int32_t kPreviousTabTextTop = 236;
constexpr int32_t kPreviousTabTextBottom = 302;
constexpr int32_t kPreviousTabTextHalf = kPreviousTabTextTop + ((kPreviousTabTextBottom - kPreviousTabTextTop) / 2);
constexpr float kPreviousTabTop = 223.0f;

// frontCheckCursorOnPreviousTab.
inline bool CursorOnPreviousTab(const MenuCursor& cursor) {
    return kTabsLeftEdge < cursor.h && kPreviousTabTop < cursor.v;
}

} // namespace Frigate::Game::Menu
