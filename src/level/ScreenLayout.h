#pragma once

#include <cstdint>

#include "level/BgView.h"
#include "level/PlayerMove.h"

// How the screen is divided between the players of a stage: fr.h's constants,
// the viewport functions of bondview.c and the z-buffer's sizes in viewport.c,
// for the US version. The split-screen decisions are in
// docs/multiplayer-split-screen.md.
namespace Frigate::Level {

// SCREEN_WIDTH and SCREEN_HEIGHT: the screen every stage draws on, which
// viGetX and viGetY give.
constexpr int32_t kScreenWidth = 320;
constexpr int32_t kScreenHeight = 240;

// bondviewGetCurrentPlayerViewportWidth, ...Height, ...Uly and
// get_curplayer_viewport_ulx as one rectangle: `player`'s share of the screen
// in a game of `playerCount`. `pauseAnimationPercent` is how far the arm is
// raised for the watch, which opens up a one-player game's letterboxed screen
// settings; a game of two or more has one layout whatever the setting.
// `cameraBuffer` (cameraBufferToggle) is Cuba's 440 by 330 camera feed, whose
// one-player view is a fixed size per screen setting.
ViewMetrics PlayerViewMetrics(int playerCount, int player, ScreenSize screen, float pauseAnimationPercent,
                              bool cameraBuffer = false);

// A size in pixels.
struct Extent {
    int32_t width = 0;
    int32_t height = 0;
};
// viGetX and viGetY: the screen the stage draws on. `hiRes` is
// cameraBufferToggle, the 440 by 330 mode Cuba's camera feeds use.
Extent ScreenExtent(bool hiRes);
// zbufAllocate: the size of the z-buffer. `hiRes` is `resolution != 0`, which
// holds for the whole of Cuba.
Extent ZBufferAllocation(int playerCount, bool hiRes);

// zbufInit: how many rows of the screen the depth image is moved back for
// `player` (0 for the players whose views sit in the top half of the
// z-buffer). Two or more players share one z-buffer of half the screen's
// height, each lower view over the same memory as the upper one above it;
// the interpreter's depth buffer is the whole screen, so the same split lands
// the lower views on the lower half.
int32_t ZBufferRowOffset(int playerCount, int player);

// The rectangle zbufClearCurrentPlayer fills, in screen rows and columns
// (inclusive): the player's half of the z-buffer.
struct ZBufferRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};
// `hiRes` is `resolution != 0` (the z-buffer's size), `cameraBuffer` the
// screen's (viGetX).
ZBufferRect ZBufferClearRect(int playerCount, int player, bool hiRes, bool cameraBuffer);

} // namespace Frigate::Level
