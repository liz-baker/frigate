#include <cmath>
#include <cstdio>

#include "StanFixture.h"
#include "level/PlayerMove.h"
#include "level/PlayerPadInput.h"
#include "level/Stan.h"

using namespace Frigate::Level;
using namespace Frigate::Platform::Input;
using namespace StanFixture;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

PadSample Sample(uint16_t buttons, int8_t x = 0, int8_t y = 0) {
    PadSample sample;
    sample.buttons = buttons;
    sample.stickX = x;
    sample.stickY = y;
    return sample;
}

// Port 1 holds `port`; the level view's single-player call.
MoveInput MoveInputFromPort(const PortState& port) {
    State pads;
    pads[0] = port;
    return BuildPlayerMoveInput(pads, 0, 1, 1);
}

void TestConversion() {
    PortState port;
    port = Advance(port, Sample(Buttons::A, 10, -20));
    port = Advance(port, Sample(Buttons::A | Buttons::Z, 30, 40));
    const MoveInput input = MoveInputFromPort(port);
    Check(input.stickX == 30 && input.stickY == 40, "the stick is the latest sample's");
    Check(input.buttons == (Buttons::A | Buttons::Z), "the buttons are the latest sample's");
    Check(input.prevStickX == 10 && input.prevStickY == -20, "the previous stick is the sample before");
    Check(input.pressed == Buttons::Z, "only the button that went down this frame is pressed");
    Check(input.stick2X == 0 && input.stick2Y == 0 && input.buttons2 == 0, "no second controller");
    Check(input.controllers == 1, "one controller");

    const MoveInput idle = MoveInputFromPort(PortState{});
    Check(idle.stickX == 0 && idle.buttons == 0 && idle.pressed == 0, "no input reads as no input");
}

// Synthetic samples drive a player the way the level view's loop does.
void TestDrivesPlayer() {
    Stan stan;
    stan.Load(MakeFloor(1000, 0, 0, 2), 1.0f);
    MoveContext context;
    context.random = [] { return uint32_t{ 0 }; };
    PlayerSpawn spawn;
    spawn.position = { 500.0f, 1.0f, 800.0f };
    spawn.look = { 0.0f, 0.0f, 1.0f };
    spawn.stanTile = 0;
    spawn.stanHeight = 0.0f;
    spawn.eyeHeight = 175.0f;
    PlayerMove player;
    player.Init(spawn, stan);

    PortState port;
    // The gait head moves the player by the forward speed the stick sets.
    for (int i = 0; i < 10; i++) {
        port = Advance(port, Sample(0, 0, 80));
        player.Tick(context, MoveInputFromPort(port), stan);
    }
    Check(player.SpeedForwards() > 1.0f, "stick forward sets a forward speed");

    const float theta = player.Theta();
    for (int i = 0; i < 10; i++) {
        port = Advance(port, Sample(0, 60, 0));
        player.Tick(context, MoveInputFromPort(port), stan);
    }
    Check(player.Theta() != theta, "stick sideways turns the player");
}

} // namespace

int main() {
    TestConversion();
    TestDrivesPlayer();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    return 0;
}
