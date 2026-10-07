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

// Registers MenuId::MultiplayerOptions' handlers: init_menu0E_mpoptions(),
// update_menu0E_mpoptions(), interface_menu0E_mpoptions() and
// constructor_menu0E_mpoptions(), the multiplayer settings on the open wallet.
// The logic is MpOptionsModel and MpOptions (MultiplayerOptions.h); the settings
// live in FrontState::multiplayer.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpOptions(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                       const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
