#include "game/menu/RunStage.h"

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/menu/MissionEnd.h"
#include "game/menu/MissionEndModel.h"
#include "game/menu/StageStart.h"
#include "game/save/SaveProgress.h"
#include "level/AttractDemo.h"
#include "level/PlayerPadInput.h"
#include "level/ResourceDemoSource.h"
#include "model/Random.h"
#include "platform/InputState.h"

namespace Frigate::Game::Menu {

RunStageHost::RunStageHost(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front, const DemoRunOptions& options)
    : resourceManager_(resourceManager), front_(front), options_(options) {
    startStage_ = [resourceManager, front](const SelectedStage& selected) -> std::unique_ptr<StageRun> {
        return LiveRun::Start(resourceManager, selected, *front);
    };
}

void RunStageHost::Init() {
    run_.reset();
    stage_.reset();
    stageFailed_ = false;
    if (front_->ramromIndex) {
        run_ = DemoRun::Start(resourceManager_, *front_->ramromIndex, options_);
    } else if (const auto selected = StartSelectedStage(*front_)) {
        spdlog::info("run stage: stage {} at difficulty {}", selected->stage, selected->difficulty);
        stage_ = startStage_(*selected);
        stageFailed_ = stage_ == nullptr;
    }
}

void RunStageHost::Interface(MenuStateMachine& menu) {
    if (stageFailed_) {
        stageFailed_ = false;
        front_->selectedStage = -1;
        menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/true);
        return;
    }
    if (stage_) {
        stage_->Step(Level::BuildPlayerMoveInput(Platform::Input::Current(), 0, Level::Stage::kPlayerCount,
                                                 Platform::Input::ControllerCount()));
        if (!stage_->Done()) {
            return;
        }
        const MissionOutcome outcome = MissionOutcomeOf(stage_->Result());
        stage_.reset();
        // init_menus_or_reset.
        front_->maybeIsInMenu = true;
        EndMission(menu, *front_, outcome);
        return;
    }
    if (run_) {
        run_->Step(Input::AnyButtonPressedThisFrame());
    }
    if (run_ && !run_->Done()) {
        return;
    }
    // bossRunTitleStage: the title stage's init_menus_or_reset picks the menu.
    if (run_ && run_->EndedByButton()) {
        front_->prevKeypresses = true;
    }
    run_.reset();
    front_->ramromIndex.reset();
    // init_menus_or_reset.
    front_->maybeIsInMenu = true;
    const MenuId next = MenuAfterTitleStageLoad(*front_);
    spdlog::info("attract: back to the {}", next == MenuId::LegalScreen ? "legal screen" : "file select");
    menu.ChangeMenu(next, /*reload=*/true);
}

Gfx* RunStageHost::Render(Gfx* dl) {
    if (stage_) {
        return stage_->Render(dl);
    }
    return run_->Render(dl);
}

void RunStageHost::WaitForFrame() const {
    if (run_) {
        run_->WaitForFrame();
    }
}

void RegisterRunStage(MenuStateMachine& menu, const std::shared_ptr<RunStageHost>& host) {
    menu.RegisterMenu(MenuId::RunStage, {
                                            .onInit = [host](MenuStateMachine&) { host->Init(); },
                                            .onInterface = [host](MenuStateMachine& m) { host->Interface(m); },
                                        });
}

bool SelectRamromToPlay(MenuStateMachine& menu, FrontState& front,
                        const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    Level::ResourceDemoSource demos(resourceManager);
    const int highestStageUnlocked = front.progress ? front.progress->HighestStageUnlockedAnyFolder() : 0;
    const auto selected = Level::SelectAttractDemo(demos, highestStageUnlocked, RandomGetNext());
    if (!selected) {
        spdlog::error("attract: the archives hold no demos");
        return false;
    }
    size_t index = selected->index;
    if (!front.demoOrder.empty()) {
        index = front.demoOrder[front.demoOrderNext++ % front.demoOrder.size()];
    }
    front.ramromIndex = index;
    // replay_recorded_ramrom_at_address.
    menu.ChangeMenu(MenuId::RunStage, /*reload=*/true);
    return true;
}

} // namespace Frigate::Game::Menu
