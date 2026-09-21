// Temporary viewer glue, see GbiF3dex.h.
#pragma once

#include <memory>
#include <optional>
#include <string>

namespace Ship {
class Context;
}

namespace Frigate::Viewer {

struct Options {
    // Resource path of the model to draw, e.g. models/chr/CarmourguardZ.
    std::optional<std::string> model;
    float distance = 1000.0f;
    // Turns texturing on for lists that request a texture. Off by default:
    // those draws currently crash the OpenGL backend, see docs/viewer.md.
    bool textures = false;
};

// Runs the frame loop until the window closes. Draws the fill-colour test
// list when no model is requested. Returns a process exit code.
int Run(const std::shared_ptr<Ship::Context>& context, const Options& options);

} // namespace Frigate::Viewer
