#pragma once

namespace Frigate::Game {
class MenuStateMachine;
}

namespace Frigate::Game::Menu {

// Registers MenuId::LegalScreen's interface handler: front.c's
// interface_menu00_legalscreen(), minus the parts frigate issue #81 puts
// out of scope for this slice (see LegalScreen.cpp).
void RegisterLegalScreen(MenuStateMachine& menu);

} // namespace Frigate::Game::Menu
