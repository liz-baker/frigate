#include "platform/InputState.h"

#include <cstdio>

#include "game/Input.h"

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr uint16_t kA = 0x8000;
constexpr uint16_t kStart = 0x1000;
constexpr uint16_t kZ = 0x2000;

PadSample Sample(uint16_t buttons, int8_t x = 0, int8_t y = 0) {
    PadSample sample;
    sample.buttons = buttons;
    sample.stickX = x;
    sample.stickY = y;
    return sample;
}

} // namespace

int main() {
    // A button is pressed only on the frame it goes down.
    {
        PortState port;
        port = Advance(port, Sample(kA));
        Check(port.pressed == kA, "A is pressed on its first frame");
        Check(port.buttons == kA, "A is held");
        port = Advance(port, Sample(kA));
        Check(port.pressed == 0, "A held for a second frame is no longer pressed");
        Check(port.buttons == kA, "A is still held");
        port = Advance(port, Sample(0));
        Check(port.pressed == 0 && port.buttons == 0, "releasing A presses nothing");
        port = Advance(port, Sample(kA));
        Check(port.pressed == kA, "A pressed again after a release");
    }

    // Buttons already down don't hide a new one.
    {
        PortState port;
        port = Advance(port, Sample(kA));
        port = Advance(port, Sample(kA | kStart));
        Check(port.pressed == kStart, "only the newly pressed button is reported");
        Check(port.prevButtons == kA, "the previous frame's buttons are kept");
        port = Advance(port, Sample(kStart | kZ));
        Check(port.pressed == kZ, "a release and a press in one frame report the press");
    }

    // The stick keeps the previous frame's reading alongside the latest.
    {
        PortState port;
        port = Advance(port, Sample(0, 40, -20));
        Check(port.stickX == 40 && port.stickY == -20, "latest stick");
        Check(port.prevStickX == 0 && port.prevStickY == 0, "no previous stick on the first frame");
        port = Advance(port, Sample(0, -80, 70));
        Check(port.stickX == -80 && port.stickY == 70, "latest stick after a second frame");
        Check(port.prevStickX == 40 && port.prevStickY == -20, "previous stick is the frame before");
    }

    // Ports advance independently.
    {
        State state{};
        std::array<PadSample, kPortCount> samples{};
        samples[0] = Sample(kA);
        samples[2] = Sample(kZ, 5, 6);
        Advance(state, samples);
        Check(state[0].pressed == kA, "port 1 pressed");
        Check(state[1].pressed == 0 && state[1].buttons == 0, "port 2 untouched");
        Check(state[2].pressed == kZ && state[2].stickX == 5 && state[2].stickY == 6, "port 3 pressed with stick");
        Check(state[3].pressed == 0, "port 4 untouched");
    }

    // The joyGet* readers apply their mask and treat a bad port as no input.
    {
        Current() = State{};
        std::array<PadSample, kPortCount> samples{};
        samples[0] = Sample(kA | kStart, 9, -9);
        samples[1] = Sample(kZ);
        Advance(Current(), samples);
        Check(GetButtons(0, kAnyButton) == (kA | kStart), "GetButtons returns everything for ANY_BUTTON");
        Check(GetButtons(0, kA) == kA, "GetButtons applies the mask");
        Check(GetButtonsPressedThisFrame(0, kStart) == kStart, "pressed reads apply the mask");
        Check(GetButtonsPressedThisFrame(1, kAnyButton) == kZ, "port 2 is readable");
        Check(GetStickX(0) == 9 && GetStickY(0) == -9, "stick readers");
        Check(GetButtons(-1, kAnyButton) == 0 && GetButtons(kPortCount, kAnyButton) == 0,
              "out-of-range ports read zero");
        Check(GetStickX(kPortCount) == 0 && GetStickY(-1) == 0, "out-of-range sticks read zero");
    }

    // Game::Input is port 1 with ANY_BUTTON: one frame per press, and the
    // other ports don't count.
    {
        Current() = State{};
        Check(!Frigate::Game::Input::AnyButtonPressedThisFrame(), "nothing pressed at start");
        std::array<PadSample, kPortCount> samples{};
        samples[1] = Sample(kA);
        Advance(Current(), samples);
        Check(!Frigate::Game::Input::AnyButtonPressedThisFrame(), "port 2 doesn't skip screens");
        samples[0] = Sample(kStart);
        Advance(Current(), samples);
        Check(Frigate::Game::Input::AnyButtonPressedThisFrame(), "port 1 press is seen");
        Advance(Current(), samples);
        Check(!Frigate::Game::Input::AnyButtonPressedThisFrame(), "a held button is seen once");
    }

    return gFailures == 0 ? 0 : 1;
}
