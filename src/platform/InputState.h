#pragma once

#include <array>
#include <cstdint>

// Per-port controller state with the semantics of the GoldenEye decomp's joy
// functions. No libultraship dependency, so the menu code and its tests use it
// without a window; the OSContPad side is in platform/Input.h.
namespace Frigate::Platform::Input {

// MAXCONTROLLERS.
constexpr int kPortCount = 4;

// ANY_BUTTON.
constexpr uint16_t kAnyButton = 0xFFFF;

// joyGetControllerCount: the ports from the first up to the first one with no
// controller, from the connected-ports bit mask.
constexpr int ControllerCount(uint8_t connected) {
    for (int port = 0; port < kPortCount; port++) {
        if ((connected & (1 << port)) == 0) {
            return port;
        }
    }
    return kPortCount;
}

// joyGetConnectedControllers (g_ConnectedControllers): the bit mask of
// connected ports. Init() (platform/Input.h) hands the storage to libultraship,
// which keeps it current; it reads 0 before that.
uint8_t ConnectedControllers();
uint8_t* ConnectedControllersStorage();

// One frame's reading of one controller.
struct PadSample {
    int8_t stickX = 0;
    int8_t stickY = 0;
    uint16_t buttons = 0;
};

struct PortState {
    // joyGetStickX / joyGetStickY and joyGetButtons(port, kAnyButton): the
    // latest sample.
    int8_t stickX = 0;
    int8_t stickY = 0;
    uint16_t buttons = 0;
    // The sample before: joy7000C174 / joy7000C284 for the stick, and the
    // buttons the edge detection compares against.
    int8_t prevStickX = 0;
    int8_t prevStickY = 0;
    uint16_t prevButtons = 0;
    // joyGetButtonsPressedThisFrame(port, kAnyButton): buttons & ~prevButtons.
    uint16_t pressed = 0;
};

using State = std::array<PortState, kPortCount>;

// joyConsumeSamples for one sample per frame: `sample` becomes the latest and
// `previous`'s latest becomes the sample before it.
PortState Advance(const PortState& previous, const PadSample& sample);
void Advance(State& state, const std::array<PadSample, kPortCount>& samples);

// What the game reads. Updated by Poll() (platform/Input.h), once per
// displayed frame.
State& Current();

// joyGetStickX, joyGetStickY, joyGetButtons, joyGetButtonsPressedThisFrame.
// `port` is zero-based; an out-of-range port reads as no input.
int8_t GetStickX(int port);
int8_t GetStickY(int port);
uint16_t GetButtons(int port, uint16_t mask);
uint16_t GetButtonsPressedThisFrame(int port, uint16_t mask);

} // namespace Frigate::Platform::Input
