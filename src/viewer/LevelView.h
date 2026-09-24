// Temporary viewer glue, see GbiF3dex.h.
// TODO(https://github.com/liz-baker/frigate/issues/108): replaced by the in-level camera driven by the ramrom playback.
#pragma once

#include <array>
#include <memory>
#include <string>

namespace Ship {
class Context;
}

namespace Frigate::Viewer {

struct LevelOptions {
    // The stage's bg directory name, e.g. bg_dam.
    std::string stage;
    // The room the camera starts in. When `position` is unset the camera
    // sits at that room's centre. Positions are in the game's world units.
    int room = 1;
    bool hasPosition = false;
    std::array<float, 3> position{};
    // The direction the camera looks in.
    std::array<float, 3> look = { 0.0f, 0.0f, -1.0f };
};

// Loads a stage and draws it from a fixed camera until the window closes.
int RunLevel(const std::shared_ptr<Ship::Context>& context, const LevelOptions& options);

} // namespace Frigate::Viewer
