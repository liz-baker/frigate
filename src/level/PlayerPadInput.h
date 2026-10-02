#pragma once

#include "level/PlayerMove.h"
#include "platform/InputState.h"

namespace Frigate::Level {

// The MoveInput lvlViewMoveTick hands the current player: the pad indexed by
// the player's own number (joyGetStickX(get_cur_playernum()) and friends), and
// for the two-controller styles the second pad at playerNum + playerCount
// (bondviewProcessInput). The multiplayer shuffle only orders the per-player
// tick and render passes; it never remaps pads.
//
// A pad that is absent, or past the fourth port, reads as no input: the
// decomp's joy functions return 0 for a port missing from g_ConnectedControllers,
// and libultraship reports an unplugged port as a zeroed pad, which reads the
// same. `controllers` is joyGetControllerCount().
MoveInput BuildPlayerMoveInput(const Platform::Input::State& pads, int playerNum, int playerCount, int controllers);

} // namespace Frigate::Level
