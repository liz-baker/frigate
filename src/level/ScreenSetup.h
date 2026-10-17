#pragma once

#include <cstdint>

#include "level/BgView.h"
#include "level/ScreenLayout.h"
#include "libultraship/libultra/gbi.h"

// What each player's render pass sets up for its share of the screen:
// viClearZBufCurrentPlayer (zbufInit and zbufClearCurrentPlayer),
// viSetupCurrentPlayerView's color image and viSetupScreensForNumPlayers. The
// split-screen decisions are in docs/multiplayer-split-screen.md.
namespace Frigate::Level {

// Placeholder addresses for the color and depth images; the interpreter
// recognises a fill whose color image is the depth image as a depth clear.
constexpr uintptr_t kFramebufferAddress = 0x00100000;
constexpr uintptr_t kZBufferAddress = 0x00200000;

// viClearZBufCurrentPlayer for `player`: the clear of the player's half of the
// z-buffer. `hiRes` is `resolution != 0` (the z-buffer's size), `cameraBuffer`
// is cameraBufferToggle (the screen's).
Gfx* ClearPlayerZBuffer(Gfx* dl, int playerCount, int player, bool hiRes, bool cameraBuffer);
// What viSetupCurrentPlayerView puts back after it: the framebuffer as the
// color image.
Gfx* SelectFramebuffer(Gfx* dl, bool cameraBuffer);

// viSetupScreensForNumPlayers: black over what the views leave uncovered.
// `vi` is the current player's view and `position` their place in the draw
// order (get_player_position_in_shuffled), which the first player paints the
// shared bars for.
Gfx* FillUncoveredScreen(Gfx* dl, const ViewMetrics& vi, int playerCount, int position);

} // namespace Frigate::Level
