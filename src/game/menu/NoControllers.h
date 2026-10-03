#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// Registers MenuId::NoControllers' handlers: constructor_menu16_nocontrollers(),
// the two lines shown when no controller is connected. Its init, update and
// interface routines do nothing, and nothing leaves the screen.
void RegisterNoControllers(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager);

} // namespace Frigate::Game::Menu
