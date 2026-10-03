#include "level/PlayerPadInput.h"

#include <cstdio>

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Platform::Input::PadSample Sample(uint16_t buttons, int8_t x, int8_t y) {
    Platform::Input::PadSample sample;
    sample.buttons = buttons;
    sample.stickX = x;
    sample.stickY = y;
    return sample;
}

// Port n holds button 1 << n and a stick of (10 * (n + 1), -10 * (n + 1)).
Platform::Input::State PadsWithDistinctInput() {
    Platform::Input::State state;
    std::array<Platform::Input::PadSample, Platform::Input::kPortCount> samples;
    for (int port = 0; port < Platform::Input::kPortCount; port++) {
        samples[port] = Sample(static_cast<uint16_t>(1 << port), static_cast<int8_t>(10 * (port + 1)),
                               static_cast<int8_t>(-10 * (port + 1)));
    }
    Platform::Input::Advance(state, samples);
    return state;
}

} // namespace

int main() {
    const Platform::Input::State pads = PadsWithDistinctInput();

    // Each player reads the pad with its own number.
    for (int player = 0; player < 4; player++) {
        const Level::MoveInput input = Level::BuildPlayerMoveInput(pads, player, 4, 4);
        Check(input.buttons == (1 << player), "player reads its own pad's buttons");
        Check(input.stickX == 10 * (player + 1) && input.stickY == -10 * (player + 1), "player reads its own stick");
        Check(input.pressed == (1 << player), "player reads its own pad's presses");
    }

    // The second controller of a two-controller style is playerNum + playerCount.
    {
        const Level::MoveInput one = Level::BuildPlayerMoveInput(pads, 0, 1, 2);
        Check(one.buttons2 == 2 && one.stick2X == 20, "single player's second pad is port 2");
        const Level::MoveInput two = Level::BuildPlayerMoveInput(pads, 1, 2, 4);
        Check(two.buttons2 == 8 && two.stick2Y == -40, "player 2 of 2 has its second pad on port 4");
        const Level::MoveInput first = Level::BuildPlayerMoveInput(pads, 0, 2, 4);
        Check(first.buttons2 == 4, "player 1 of 2 has its second pad on port 3");
    }

    // A second pad past the fourth port reads as idle.
    {
        const Level::MoveInput input = Level::BuildPlayerMoveInput(pads, 2, 3, 4);
        Check(input.buttons2 == 0 && input.stick2X == 0 && input.stick2Y == 0, "out-of-range second pad is idle");
        Check(input.buttons == 4, "the first pad still reads");
    }
    {
        const Level::MoveInput input = Level::BuildPlayerMoveInput(pads, -1, 1, 1);
        Check(input.buttons == 0 && input.stickX == 0, "out-of-range player is idle");
    }

    // An absent controller is a zeroed pad: no input, nothing pressed.
    {
        Platform::Input::State absent = pads;
        absent[1] = Platform::Input::PortState{};
        const Level::MoveInput input = Level::BuildPlayerMoveInput(absent, 1, 2, 1);
        Check(input.buttons == 0 && input.stickX == 0 && input.pressed == 0, "absent pad reads as no input");
        Check(input.controllers == 1, "the controller count passes through");
    }

    // The previous stick and the edge come from the player's own pad.
    {
        Platform::Input::State state = pads;
        std::array<Platform::Input::PadSample, Platform::Input::kPortCount> samples;
        samples[1] = Sample(0x0002, 5, 6);
        Platform::Input::Advance(state, samples);
        const Level::MoveInput input = Level::BuildPlayerMoveInput(state, 1, 2, 2);
        Check(input.prevStickX == 20 && input.prevStickY == -20, "previous stick is the player's own");
        Check(input.pressed == 0, "a held button is not pressed again");
        Check(input.stickX == 5 && input.stickY == 6, "latest stick is the new sample");
    }

    return gFailures == 0 ? 0 : 1;
}
