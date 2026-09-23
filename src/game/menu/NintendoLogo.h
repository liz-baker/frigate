#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
}

namespace Frigate::Game::Menu {

// Registers MenuId::NintendoLogo's onInit/onConstruct handlers:
// init_menu01_nintendo()/constructor_menu01_nintendo()'s model instancing
// and spin animation. No further auto-advance is registered -- chaining on
// to the Rareware logo screen is later front-end work.
void RegisterNintendoLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager);

} // namespace Frigate::Game::Menu
