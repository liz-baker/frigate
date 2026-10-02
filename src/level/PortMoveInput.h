#pragma once

#include "level/PlayerMove.h"
#include "platform/InputState.h"

namespace Frigate::Level {

// The first controller's polled state as the tick's input: the stick and
// buttons of the latest sample, the stick before it, and the buttons that went
// down this frame. The second controller and the controller count stay at
// their single-controller defaults.
inline MoveInput MoveInputFromPort(const Platform::Input::PortState& port) {
    MoveInput input;
    input.stickX = port.stickX;
    input.stickY = port.stickY;
    input.buttons = port.buttons;
    input.prevStickX = port.prevStickX;
    input.prevStickY = port.prevStickY;
    input.pressed = port.pressed;
    return input;
}

} // namespace Frigate::Level
