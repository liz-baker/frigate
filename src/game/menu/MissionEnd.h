#pragma once

#include <memory>

#include "game/MenuId.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

struct MissionOutcome;

// Registers MenuId::MissionFailed's and MenuId::MissionComplete's handlers
// (init_menu0C_missionfailed(), interface_menu0C_missionfailed(),
// constructor_menu0C_missionfailed() and the menu0D_missioncomplete trio): the
// report page, which every solo stage returns to, and the statistics page it
// leads on to. The logic is MissionFailedModel and MissionCompleteModel.
void RegisterMissionEnd(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

// What bossReturnTitleStage and the run-stage menu's interface do when a solo
// stage ends: the completed objectives are recorded into the folder
// (end_of_mission_briefing), then the Cuba stage goes to the credits cast and
// any other stage to the report page. The briefing page and difficulty selected
// for the stage are the ones the report is about.
//
// RunStageHost calls this when a stage played from the menus ends. A mission
// is recorded only if the outcome says bossReturnTitleStage ran with every
// objective complete.
void EndMission(MenuStateMachine& menu, FrontState& front, const MissionOutcome& outcome);

// The report pages a preview can open on.
enum class MissionEndPreview { Failed, KilledInAction, Aborted, Completed, Statistics };

// A testing aid: selects Dam at 00 Agent and a canned outcome, and returns the
// menu to start on.
MenuId PrepareMissionEndPreview(FrontState& front, MissionEndPreview preview);

} // namespace Frigate::Game::Menu
