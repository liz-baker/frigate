#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
}

namespace Frigate::Game::Menu {

// Registers MenuId::LegalScreen's handlers:
// - onInterface: front.c's interface_menu00_legalscreen(), minus the parts
//   frigate issue #81 puts out of scope for this slice (controller input,
//   #40).
// - onInit/onConstruct: init_menu00_legalscreen()/constructor_menu00_legalscreen()'s
//   model instancing and drawing (frigate issue #85) -- PROP_LEGALPAGE has
//   no animation, so the model is built once and redrawn unchanged every
//   frame. No text yet (#83).
void RegisterLegalScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager);

} // namespace Frigate::Game::Menu
