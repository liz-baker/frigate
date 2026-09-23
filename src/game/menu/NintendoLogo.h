#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
}

namespace Frigate::Game::Menu {

// Registers MenuId::NintendoLogo's onInit/onConstruct handlers: front.c's
// init_menu01_nintendo()/constructor_menu01_nintendo()'s model instancing
// and spin animation (frigate issue #85) -- rotation only, no
// skeleton/joint animation. No further auto-advance is registered:
// chaining on to the Rareware logo screen is later front-end work, out of
// #81/#84/#85's scope.
void RegisterNintendoLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager);

} // namespace Frigate::Game::Menu
