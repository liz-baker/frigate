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

// Registers MenuId::MultiplayerCharacterSelect's handlers:
// init_menu0f_mpcharsel(), interface_menu0F_mpcharsel() and
// constructor_menu0F_mpcharsel(), a strip of character portraits for each of
// the players. update_menu0F_mpcharsel() does nothing. The logic is
// MpCharSelectModel; the choices live in FrontState::multiplayer.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpCharSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                          const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
