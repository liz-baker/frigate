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

// Registers MenuId::Cheat's handlers: init_menu15_cheat(),
// update_menu15_cheat(), interface_menu15_cheat() and
// constructor_menu15_cheat(), the list of cheats the selected folder has
// unlocked with each one's on or off state, over the wallet backdrop. The
// logic is CheatMenuModel. The mode select fills FrontState::cheatAvailable
// (IsCheatAvailable) before this screen starts. The previous tab goes back to
// MenuId::ModeSelect.
//
// The cheats switched on here (FrontState::cheatActivated) are applied when a
// stage loads, by LiveRun.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterCheatMenu(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                       const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
