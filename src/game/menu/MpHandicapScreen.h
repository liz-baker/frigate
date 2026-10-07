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

// Registers MenuId::MultiplayerHandicap's handlers: init_menu10_mphandicap(),
// interface_menu10_mphandicap() and constructor_menu10_mphandicap(), a
// handicap for each of the players. update_menu10_mphandicap() does nothing.
// The logic is MpHandicapModel; the choices live in
// FrontState::multiplayer.handicap.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpHandicap(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
