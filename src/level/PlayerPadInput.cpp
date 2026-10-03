#include "level/PlayerPadInput.h"

namespace Frigate::Level {

namespace {

using Platform::Input::PortState;

const PortState& Port(const Platform::Input::State& pads, int port) {
    static const PortState idle;
    return port >= 0 && port < Platform::Input::kPortCount ? pads[port] : idle;
}

} // namespace

MoveInput BuildPlayerMoveInput(const Platform::Input::State& pads, int playerNum, int playerCount, int controllers) {
    const PortState& first = Port(pads, playerNum);
    const PortState& second = Port(pads, playerNum + playerCount);
    MoveInput input;
    input.stickX = first.stickX;
    input.stickY = first.stickY;
    input.buttons = first.buttons;
    input.stick2X = second.stickX;
    input.stick2Y = second.stickY;
    input.buttons2 = second.buttons;
    input.prevStickX = first.prevStickX;
    input.prevStickY = first.prevStickY;
    input.pressed = first.pressed;
    input.controllers = controllers;
    return input;
}

} // namespace Frigate::Level
