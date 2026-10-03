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

// Registers MenuId::MissionSelect's handlers: init_menu07_missionselect(),
// interface_menu07_missionsel() and constructor_menu07_missionsel(), the
// folder's twenty mission icons with the crosshair cursor. The logic is
// MissionSelectModel. Choosing a mission continues to MenuId::Difficulty, and
// Previous or B returns to MenuId::ModeSelect.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMissionSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
