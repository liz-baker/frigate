#pragma once

#include <cstdint>

#include "platform/InputState.h"

namespace Frigate::Game::Input {

// The N64 button masks joyGetButtonsPressedThisFrame() is called with.
namespace Button {
constexpr uint16_t kCRight = 0x0001;
constexpr uint16_t kCLeft = 0x0002;
constexpr uint16_t kCDown = 0x0004;
constexpr uint16_t kCUp = 0x0008;
constexpr uint16_t kR = 0x0010;
constexpr uint16_t kL = 0x0020;
constexpr uint16_t kPadRight = 0x0100;
constexpr uint16_t kPadLeft = 0x0200;
constexpr uint16_t kPadDown = 0x0400;
constexpr uint16_t kPadUp = 0x0800;
constexpr uint16_t kStart = 0x1000;
constexpr uint16_t kZ = 0x2000;
constexpr uint16_t kB = 0x4000;
constexpr uint16_t kA = 0x8000;
} // namespace Button

// One frame's reading of the first controller: the buttons that went down
// this frame (joyGetButtonsPressedThisFrame), the buttons held (joyGetButtons),
// and the stick now and at the sample before (joyGetStickX/Y, joy7000C174 and
// joy7000C284). Screens take this by value, so their logic runs without a
// controller.
struct Sample {
    uint16_t pressed = 0;
    uint16_t held = 0;
    int8_t stickX = 0;
    int8_t stickY = 0;
    int8_t prevStickX = 0;
    int8_t prevStickY = 0;

    // joyGetButtonsPressedThisFrame(PLAYER_1, mask) != 0.
    bool Pressed(uint16_t mask) const {
        return (pressed & mask) != 0;
    }
    bool Held(uint16_t mask) const {
        return (held & mask) != 0;
    }
    // The stick moved past `threshold` this sample from at or inside it, in the
    // positive or negative direction of the axis.
    static bool CrossedAbove(int8_t previous, int8_t current, int threshold) {
        return previous <= threshold && current > threshold;
    }
    static bool CrossedBelow(int8_t previous, int8_t current, int threshold) {
        return previous >= -threshold && current < -threshold;
    }
    bool StickXCrossedAbove(int threshold) const {
        return CrossedAbove(prevStickX, stickX, threshold);
    }
    bool StickXCrossedBelow(int threshold) const {
        return CrossedBelow(prevStickX, stickX, threshold);
    }
    bool StickYCrossedAbove(int threshold) const {
        return CrossedAbove(prevStickY, stickY, threshold);
    }
    bool StickYCrossedBelow(int threshold) const {
        return CrossedBelow(prevStickY, stickY, threshold);
    }

    static Sample FromPort(const Platform::Input::PortState& port) {
        return { .pressed = port.pressed,
                 .held = port.buttons,
                 .stickX = port.stickX,
                 .stickY = port.stickY,
                 .prevStickX = port.prevStickX,
                 .prevStickY = port.prevStickY };
    }
};

// The first controller's reading for this frame.
inline Sample ReadPlayer1() {
    return Sample::FromPort(Platform::Input::Current()[0]);
}

// joyGetControllerCount().
inline int ControllerCount() {
    return Platform::Input::ControllerCount();
}

// joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON).
inline bool AnyButtonPressedThisFrame() {
    return Platform::Input::GetButtonsPressedThisFrame(0, Platform::Input::kAnyButton) != 0;
}

} // namespace Frigate::Game::Input
