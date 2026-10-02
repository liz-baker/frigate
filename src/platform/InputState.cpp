#include "platform/InputState.h"

namespace Frigate::Platform::Input {

PortState Advance(const PortState& previous, const PadSample& sample) {
    PortState next;
    next.stickX = sample.stickX;
    next.stickY = sample.stickY;
    next.buttons = sample.buttons;
    next.prevStickX = previous.stickX;
    next.prevStickY = previous.stickY;
    next.prevButtons = previous.buttons;
    next.pressed = static_cast<uint16_t>(sample.buttons & ~previous.buttons);
    return next;
}

void Advance(State& state, const std::array<PadSample, kPortCount>& samples) {
    for (int port = 0; port < kPortCount; port++) {
        state[port] = Advance(state[port], samples[port]);
    }
}

State& Current() {
    static State state;
    return state;
}

namespace {

const PortState* Port(int port) {
    return port >= 0 && port < kPortCount ? &Current()[port] : nullptr;
}

} // namespace

int8_t GetStickX(int port) {
    const PortState* state = Port(port);
    return state ? state->stickX : 0;
}

int8_t GetStickY(int port) {
    const PortState* state = Port(port);
    return state ? state->stickY : 0;
}

uint16_t GetButtons(int port, uint16_t mask) {
    const PortState* state = Port(port);
    return state ? static_cast<uint16_t>(state->buttons & mask) : 0;
}

uint16_t GetButtonsPressedThisFrame(int port, uint16_t mask) {
    const PortState* state = Port(port);
    return state ? static_cast<uint16_t>(state->pressed & mask) : 0;
}

} // namespace Frigate::Platform::Input
