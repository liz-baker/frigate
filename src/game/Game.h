#pragma once

#include <memory>

namespace Ship {
class Context;
}

namespace Frigate::Game {

// Frigate's real game loop -- distinct from src/viewer/'s throwaway
// asset-inspection scaffolding (docs/viewer.md), and the seed later
// menu/scene work builds on. Ticks the menu state machine (see
// MenuStateMachine.h) once per frame and draws whatever it produces.
int Run(const std::shared_ptr<Ship::Context>& context);

} // namespace Frigate::Game
