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

// Registers MenuId::MultiplayerScenarioSelect's handlers:
// init_menu13_mpscenariosel(), update_menu13_mpscenario() (which does nothing),
// interface_menu13_mpscenario() and constructor_menu13_mpscenario(), the list of
// scenarios on the open wallet. The logic is MpScenarioSelectModel; the choice
// lives in FrontState::multiplayer.scenario.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpScenarioSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                              const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
