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

// Registers MenuId::MultiplayerControlStyle's handlers: init_menu11_mpcontrol(),
// interface_menu11_mpcontrols() and constructor_menu11_mpcontrol(), a control
// style for each of the players. update_menu11_mpcontrols() does nothing. The
// logic is MpControlStyleModel; the choices live in
// FrontState::multiplayer.controlStyle.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpControlStyle(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                    const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
