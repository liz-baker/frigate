#include "level/PlayerPadInput.h"

namespace Frigate::Level {

namespace {

using Platform::Input::PortState;

const PortState& Port(const Platform::Input::State& pads, int port) {
    static const PortState idle;
    return port >= 0 && port < Platform::Input::kPortCount ? pads[port] : idle;
}

const Platform::Input::ModernFrame& ModernPort(const Platform::Input::ModernState& modern, int port) {
    static const Platform::Input::ModernFrame off;
    return port >= 0 && port < Platform::Input::kPortCount ? modern[port] : off;
}

} // namespace

MoveInput BuildPlayerMoveInput(const Platform::Input::State& pads, int playerNum, int playerCount, int controllers,
                               const Platform::Input::ModernState& modern) {
    const PortState& first = Port(pads, playerNum);
    const PortState& second = Port(pads, playerNum + playerCount);
    const Platform::Input::ModernFrame& frame = ModernPort(modern, playerNum);
    MoveInput input;
    input.stickX = first.stickX;
    input.stickY = first.stickY;
    input.buttons = first.buttons;
    input.prevStickX = first.prevStickX;
    input.prevStickY = first.prevStickY;
    input.pressed = first.pressed;
    input.controllers = controllers;
    if (!frame.active) {
        input.stick2X = second.stickX;
        input.stick2Y = second.stickY;
        input.buttons2 = second.buttons;
        return input;
    }
    input.modern = true;
    input.moveX = frame.moveX;
    input.moveY = frame.moveY;
    input.lookYaw = frame.lookYaw;
    input.lookPitch = frame.lookPitch;
    input.lookYawRate = frame.lookYawRate;
    input.lookPitchRate = frame.lookPitchRate;
    input.detonate = frame.detonate;
    input.interactPressed = frame.interactPressed;
    input.reloadPressed = frame.reloadPressed;
    input.crouchMode = frame.crouchToggle ? CrouchMode::Toggle : CrouchMode::Hold;
    input.crouchHeld = frame.crouchHeld;
    return input;
}

} // namespace Frigate::Level
