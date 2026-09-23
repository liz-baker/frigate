#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// Registers MenuId::NintendoLogo's handlers: init_menu01_nintendo(),
// update_menu01_nintendo(), interface_menu01_nintendo() (minus controller
// input, #40) and constructor_menu01_nintendo()'s spin animation.
void RegisterNintendoLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                          const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
