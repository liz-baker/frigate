#pragma once

#include <array>
#include <cstdint>

#include "game/menu/CheatIds.h"

namespace Frigate::Level {
class Stage;
}

// The Dev Tools tab's switches for the cheats of the stage being played. The
// menu asks; the game loop applies the asks to the stage between frames and
// publishes what the stage then has on.
namespace Frigate::Game::DevCheats {

struct Snapshot {
    // A stage is being played (not an attract demo).
    bool live = false;
    int playerCount = 0;
    // cheatIsActive for the first player, by cheat id.
    std::array<bool, Menu::Cheat::kInvalid> active{};
};

// The stage's cheats as of the last Sync.
Snapshot Current();

// Asks for `cheat` to be switched on or off in the stage being played. Dropped
// when no stage is being played by the next Sync.
void Request(int32_t cheat, bool on);

// Game loop side, once a frame: applies the asks to `stage` and publishes its
// state. `stage` is null when no stage is being played.
void Sync(Level::Stage* stage);

// Whether `cheat` does anything when switched on in a stage of `playerCount`
// players: the cheat's flags have to include that kind of game. Enemy rockets
// are flagged for the menus only, but they are a stage's cheat.
bool AppliesInStage(int32_t cheat, int playerCount);

} // namespace Frigate::Game::DevCheats
