// Loads a stage or attract demo and runs it outside the front end (developer builds only).
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "level/PlayerMove.h"

namespace Ship {
class Context;
}

namespace Frigate::Dev {

struct LevelOptions {
    // The stage's bg directory name, e.g. bg_dam.
    std::string stage;
    // Without `room`, `position` and `look` the camera starts at the stage's
    // spawn. Positions are in the game's world units.
    std::optional<int> room;
    bool hasPosition = false;
    std::array<float, 3> position{};
    // The direction the camera looks in.
    bool hasLook = false;
    std::array<float, 3> look{};
    // The demo whose spawn and starting items the stage loads with; 0 is
    // normal play.
    int demoSlot = 0;
    // The players the stage loads for (the multiplayer defaults for two or
    // more); only player 1's view is drawn.
    int players = 1;
    // Plays attract-mode demo `demo` (an index into the ramrom table): its
    // stage and slot replace `stage` and `demoSlot`, and the camera follows
    // the player its recorded input drives.
    std::optional<int> demo;
    // The control style live play uses ("1.1" to "2.4"), as the save file's
    // option bits would set it; the game itself reads the selected folder's
    // save.
    std::optional<Level::ControlConfig> controlStyle;
    // Fade to the title on a random-byte mismatch like the game (off: only
    // log it), and play the demo as fast as it draws.
    bool fadeOnRandomMismatch = true;
    bool uncapped = false;
};

// Loads a stage and draws it until the window closes: through the player the
// first controller drives, or from a fixed camera when --room, --camera or
// --look places one.
int RunLevel(const std::shared_ptr<Ship::Context>& context, const LevelOptions& options);

} // namespace Frigate::Dev
