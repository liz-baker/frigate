// Temporary viewer glue, see GbiF3dex.h.
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace Ship {
class Context;
}

namespace Frigate::Viewer {

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
    // Plays attract-mode demo `demo` (an index into the ramrom table): its
    // stage and slot replace `stage` and `demoSlot`, and the camera follows
    // the player its recorded input drives.
    std::optional<int> demo;
};

// Loads a stage and draws it from a fixed camera until the window closes.
int RunLevel(const std::shared_ptr<Ship::Context>& context, const LevelOptions& options);

} // namespace Frigate::Viewer
