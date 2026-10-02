#include "game/menu/RunStage.h"

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "level/AttractDemo.h"
#include "level/ResourceDemoSource.h"
#include "model/Random.h"

namespace Frigate::Game::Menu {

RunStageHost::RunStageHost(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front, const DemoRunOptions& options)
    : resourceManager_(resourceManager), front_(front), options_(options) {
}

void RunStageHost::Init() {
    run_.reset();
    if (front_->ramromIndex) {
        run_ = DemoRun::Start(resourceManager_, *front_->ramromIndex, options_);
    }
}

void RunStageHost::Interface(MenuStateMachine& menu) {
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
    // TODO(https://github.com/liz-baker/frigate/issues/115):
    // fileGetHighestStageUnlockedAnyFolder from the save data. Every demo in
    // the table is unlocked from stage 0, so the pick is the same either way.
    const auto selected = Level::SelectAttractDemo(demos, /*highestStageUnlocked=*/0, RandomGetNext());
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
