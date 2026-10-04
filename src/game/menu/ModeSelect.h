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

// Registers MenuId::ModeSelect's handlers: init_menu06_modeselect(),
// update_menu06_modesel(), interface_menu06_modesel() and
// constructor_menu06_modesel(), the single player, multiplayer and cheat
// choices on the open wallet. The logic is ModeSelectModel. Single player
// continues to MenuId::MissionSelect, multiplayer to
// MenuId::MultiplayerOptions, cheats to MenuId::Cheat and the Previous tab to
// MenuId::FileSelect.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterModeSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
