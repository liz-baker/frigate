#pragma once

#include "platform/InputState.h"

namespace Frigate::Game::Input {

// joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON).
inline bool AnyButtonPressedThisFrame() {
    return Platform::Input::GetButtonsPressedThisFrame(0, Platform::Input::kAnyButton) != 0;
}

} // namespace Frigate::Game::Input
