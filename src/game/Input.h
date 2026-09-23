#pragma once

namespace Frigate::Game::Input {

// joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON).
// TODO(https://github.com/liz-baker/frigate/issues/40): wire to ControlDeck;
// until then no button is ever pressed, so the menus' skip-ahead branches
// are ported but unreachable.
inline bool AnyButtonPressedThisFrame() {
    return false;
}

} // namespace Frigate::Game::Input
