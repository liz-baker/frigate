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

// A player under `style` after one frame of the two pads, built the way the
// level view builds its input.
struct StyleStep {
    bool aiming;
    float sideways;
    float forwards;
    MoveData move;
};

StyleStep StepStyle(ControlConfig style, const PadSample& first, const PadSample& second, int controllers) {
    Stan stan;
    stan.Load(MakeFloor(1000, 0, 0, 2), 1.0f);
    MoveContext context;
    context.random = [] { return uint32_t{ 0 }; };
    context.options.controlType = style;
    PlayerSpawn spawn;
    spawn.position = { 500.0f, 1.0f, 800.0f };
    spawn.look = { 0.0f, 0.0f, 1.0f };
    spawn.stanTile = 0;
    spawn.stanHeight = 0.0f;
    spawn.eyeHeight = 175.0f;
    PlayerMove player;
    player.Init(spawn, stan);

    State pads;
    pads[0] = Advance(PortState{}, first);
    pads[1] = Advance(PortState{}, second);
    player.Tick(context, BuildPlayerMoveInput(pads, 0, 1, controllers), stan);
    return { player.InSightAim(), player.SpeedSideways(), player.SpeedForwards(), player.LastMove() };
}

// The pads reach each style as the decomp's joy reads: the first pad's stick
// and buttons, and for 2.1 to 2.4 the second pad's stick and A, B and Z.
void TestStylesFromPads() {
    const PadSample idle;
    const ControlConfig twoPadStyles[] = { ControlConfig::Plenty, ControlConfig::Galore, ControlConfig::Domino,
                                           ControlConfig::Goodhead };
    for (const ControlConfig style : twoPadStyles) {
        const bool secondAims = style == ControlConfig::Plenty || style == ControlConfig::Galore;
        // Port 2's Z aims or shoots, and its stick strafes.
        const StyleStep two = StepStyle(style, idle, Sample(Buttons::Z, 40, 0), 2);
        Check(two.aiming == secondAims, "port 2's Z is the aim button in 2.1 and 2.2, the trigger otherwise");
        Check(two.sideways > 0.0f, "port 2's stick strafes");
        // The same pad with no controller connected, as an unplugged port: zeroed.
        const StyleStep none = StepStyle(style, idle, idle, 1);
        Check(!none.aiming && none.sideways == 0.0f, "an unplugged port 2 reads as no input");
        // Port 2's C buttons are not read by any style.
        const StyleStep cButtons = StepStyle(style, idle, Sample(Buttons::LeftC | Buttons::UpC | Buttons::L), 2);
        Check(!cButtons.aiming && !cButtons.move.digitalStepLeft && cButtons.sideways == 0.0f,
              "port 2's C buttons and L do nothing");
    }

    // Under a one-controller style the second pad is never read.
    const StyleStep honey = StepStyle(ControlConfig::Honey, idle, Sample(Buttons::Z | Buttons::A, 80, 80), 2);
    Check(!honey.aiming && honey.sideways == 0.0f && honey.forwards == 0.0f, "port 2 does nothing in 1.1");
    // 2.1: the first pad's stick walks while the second strafes.
    const StyleStep plenty = StepStyle(ControlConfig::Plenty, Sample(0, 0, 80), Sample(0, 40, 0), 2);
    Check(plenty.forwards > 0.0f && plenty.sideways > 0.0f, "2.1 walks on port 1 and strafes on port 2");
}

} // namespace

int main() {
    TestConversion();
    TestDrivesPlayer();
    TestStylesFromPads();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    return 0;
}
