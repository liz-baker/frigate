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

// Registers MenuId::MultiplayerTeams' handlers: init_menu14_mpteamsel(),
// interface_menu14_mpteams() and constructor_menu14_mpteams(), the team each
// player is on. update_menu14_mpteams() does nothing. The logic is
// MpTeamsModel; the choice lives in FrontState::multiplayer.teamSize.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpTeams(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                    const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
