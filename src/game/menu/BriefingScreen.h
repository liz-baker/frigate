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

// Registers MenuId::Briefing's handlers: init_menu0A_briefing(),
// update_menu0A_briefing(), interface_menu0A_briefing() and
// constructor_menu0A_briefing(), the wallet's briefing pages for the stage in
// FrontState::briefingPage. START, or NEXT on the last page, continues to
// MenuId::RunStage; PREVIOUS on the first page goes back to
// MenuId::MissionSelect. The logic is BriefingModel.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterBriefing(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                      const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
