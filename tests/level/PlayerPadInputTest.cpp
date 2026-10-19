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

    // Modern ports: only a player whose own port is active gets the synthesized input.
    {
        const Platform::Input::State pads = PadsWithDistinctInput();
        Platform::Input::ModernState modern;
        modern[1].active = true;
        modern[1].moveX = 40;
        modern[1].moveY = -30;
        modern[1].lookYaw = 2.0f;
        modern[1].lookPitch = -1.0f;
        modern[1].lookYawRate = 0.5f;
        modern[1].lookPitchRate = -0.25f;
        modern[1].crouchHeld = true;
        modern[1].interactPressed = true;
        modern[1].reloadPressed = true;
        modern[1].detonate = true;

        const Level::MoveInput first = Level::BuildPlayerMoveInput(pads, 0, 2, 4, modern);
        Check(!first.modern && first.stickX == 10 && first.moveX == 0 && first.crouchMode == Level::CrouchMode::None &&
                  first.stick2X == 30,
              "a player on an N64 port reads as before, second pad included");

        const Level::MoveInput second = Level::BuildPlayerMoveInput(pads, 1, 2, 4, modern);
        Check(second.modern && second.moveX == 40 && second.moveY == -30, "a Modern port's player gets its move stick");
        Check(second.lookYaw == 2.0f && second.lookPitch == -1.0f && second.lookYawRate == 0.5f &&
                  second.lookPitchRate == -0.25f,
              "and its look");
        Check(second.interactPressed && second.reloadPressed && second.detonate && second.crouchHeld,
              "and its actions");
        Check(second.crouchMode == Level::CrouchMode::Hold, "crouch is held unless the port toggles");
        Check(second.stickX == 20 && second.buttons == 2 && second.stick2X == 0 && second.stick2Y == 0 &&
                  second.buttons2 == 0,
              "the pad is the port's synthesized one and no second pad is read");
        modern[1].crouchToggle = true;
        Check(Level::BuildPlayerMoveInput(pads, 1, 2, 4, modern).crouchMode == Level::CrouchMode::Toggle,
              "a toggling port asks for Toggle");

        const Level::MoveInput off = Level::BuildPlayerMoveInput(pads, 1, 2, 4);
        const Level::MoveInput none = Level::BuildPlayerMoveInput(pads, 1, 2, 4, Platform::Input::kNoModernInput);
        Check(!off.modern && !none.modern && off.stick2X == 40 && none.stickX == off.stickX,
              "without Modern input nothing changes");
    }

    return gFailures == 0 ? 0 : 1;
}
