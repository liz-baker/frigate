#pragma once

#include <cstdint>
#include <string_view>

#include "game/FrontState.h"

union Gfx;

namespace Frigate {
class Font;
}

namespace Frigate::Game::Menu {

// The sound effects the folder screens raise for their tabs and choices
// (sndPlaySfx), in the order a model raises them.
enum class FolderSfx {
    DoorMetalClose,
    DoorMetalClose2,
    PaperTurn,
};

// front.h's tab geometry: the START, NEXT and PREVIOUS tabs down the folder's
// right edge, in the front-end's 440x330 screen space.
namespace FolderTab {
constexpr float kLeftEdge = 390.0f;
constexpr int32_t kRightEdge = 411;
constexpr float kNextCursorH = 399.0f;
constexpr float kNextCursorV = 144.0f;

constexpr int32_t kStartTextTop = 51;
constexpr int32_t kStartTextBottom = 117;
constexpr float kStartTabBottom = 130.5f;
constexpr float kNextTabTop = 130.5f;
constexpr int32_t kNextTextTop = 144;
constexpr int32_t kNextTextBottom = 210;
constexpr float kPreviousTabTop = 223.0f;
constexpr int32_t kPreviousTextTop = 236;
constexpr int32_t kPreviousTextBottom = 302;
} // namespace FolderTab

// frontCheckCursorOnStartTab, frontCheckCursorOnNextTab and
// frontCheckCursorOnPreviousTab.
inline bool CursorOnStartTab(const MenuCursor& cursor) {
    return FolderTab::kLeftEdge < cursor.h && cursor.v <= FolderTab::kStartTabBottom;
}
inline bool CursorOnNextTab(const MenuCursor& cursor) {
    return FolderTab::kLeftEdge < cursor.h && FolderTab::kNextTabTop < cursor.v &&
           cursor.v <= FolderTab::kPreviousTabTop;
}
inline bool CursorOnPreviousTab(const MenuCursor& cursor) {
    return FolderTab::kLeftEdge < cursor.h && FolderTab::kPreviousTabTop < cursor.v;
}

// frontSetCursorPositionToNextTab.
inline void SetCursorToNextTab(MenuCursor& cursor) {
    cursor.h = FolderTab::kNextCursorH;
    cursor.v = FolderTab::kNextCursorV;
}

// frontAddStartTabText, frontAddNextTabText and frontAddPreviousTabText: the
// tab's caption in the gothic font turned a quarter turn, on a dark box when
// the tab is highlighted. `text` is the LTITLE string. `font` must outlive the
// display list.
Gfx* DrawStartTab(Gfx* dl, const Font& font, std::string_view text, bool highlighted);
Gfx* DrawNextTab(Gfx* dl, const Font& font, std::string_view text, bool highlighted);
Gfx* DrawPreviousTab(Gfx* dl, const Font& font, std::string_view text, bool highlighted);

} // namespace Frigate::Game::Menu
