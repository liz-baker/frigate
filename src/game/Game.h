#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace Ship {
class Context;
}

namespace Frigate::Game {

struct Options {
    // Play these ramrom table entries in turn instead of the game's random
    // pick (a testing aid).
    std::vector<size_t> demoOrder;
    // ramrom_replay_handler's fade to the title when the recorded random byte
    // differs from the game's. Off: a mismatch is only logged.
    bool fadeOnRandomMismatch = false;
    // Run the menus and demos as fast as they draw instead of at the
    // console's frame rate.
    bool uncapped = false;
};

// Frigate's real game loop -- distinct from src/viewer/'s throwaway
// asset-inspection scaffolding (docs/viewer.md), and the seed later
// menu/scene work builds on. Ticks the menu state machine (see
// MenuStateMachine.h) once per frame and draws whatever it produces.
int Run(const std::shared_ptr<Ship::Context>& context, const Options& options = {});

} // namespace Frigate::Game
