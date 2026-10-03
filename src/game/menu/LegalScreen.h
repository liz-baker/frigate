#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
}

namespace Frigate::Game::Save {
class SaveManager;
}

namespace Frigate::Game::Menu {

// Registers MenuId::LegalScreen's handlers: interface_menu00_legalscreen()
// (minus controller input, #40), and init_menu00_legalscreen()/
// constructor_menu00_legalscreen()'s model instancing and drawing.
// PROP_LEGALPAGE has no animation, so the model is built once and redrawn
// unchanged every frame; the LTITLE strings are drawn over it. init_menu00_
// legalscreen also runs fileValidateSaves(), which `save` stands in for.
void RegisterLegalScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                         const std::shared_ptr<Save::SaveManager>& save);

} // namespace Frigate::Game::Menu
