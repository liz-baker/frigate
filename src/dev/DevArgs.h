// Developer-only launch arguments (FRIGATE_DEV_TOOLS builds): test harness
// modes, demo and stage launching, quick-start. Release builds accept only
// the archive and extraction arguments main.cpp parses itself.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "dev/LevelRun.h"
#include "game/Game.h"
#include "game/MultiplayerQuickStart.h"

namespace Ship {
class Context;
}

namespace Frigate::Dev {

struct Args {
    // --play-sound plays one sound effect or music track and exits, with no window.
    std::optional<std::string> playSound;
    // Seconds a music track plays for under --play-sound; 0 loops until Ctrl-C.
    double playSeconds = 0.0;
    std::optional<LevelOptions> level;
    Game::Options game;
    // --multiplayer starts a multiplayer game without the menus
    // (docs/multiplayer-quick-start.md); --players is also the level runner's player count.
    bool multiplayer = false;
    std::optional<int> players;
    Game::QuickStartArgs quickStart;
};

enum class ParseStatus {
    // argv[i] is not a developer argument (or its value is missing).
    NotMine,
    // Handled; `i` has moved past any value it took.
    Consumed,
    // Handled and rejected; the reason is logged.
    Error,
};

ParseStatus ParseArg(int argc, char** argv, int& i, Args& args);

// Checks the arguments against each other and builds the quick-start game.
// False when they conflict; the reason is logged.
bool Finish(Args& args);

// The usage text, including the arguments main.cpp parses itself.
const char* Usage();

// Plays the sound `--play-sound` named and exits the process; returns when it
// was not asked for.
void RunSoundTestIfRequested(const std::shared_ptr<Ship::Context>& context, const Args& args);

} // namespace Frigate::Dev
