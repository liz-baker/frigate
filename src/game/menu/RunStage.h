#pragma once

#include <memory>
#include <utility>

#include "game/DemoRun.h"
#include "game/LiveRun.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// The stage MenuId::RunStage plays: the game loop draws it instead of the
// front-end's own frame while it is running.
class RunStageHost {
  public:
    RunStageHost(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                 const std::shared_ptr<FrontState>& front, const DemoRunOptions& options);

    // Replaces how a stage the menus selected is loaded and played.
    void SetStageFactory(StageRunFactory factory) {
        startStage_ = std::move(factory);
    }

    // A demo or a stage is loaded and playing.
    bool Active() const {
        return run_ != nullptr || stage_ != nullptr;
    }
    // A stage the player is playing, as opposed to an attract demo.
    bool Live() const {
        return stage_ != nullptr;
    }
    // The stage the player is playing; null for a demo or none.
    Level::Stage* PlayedStage() const {
        return stage_ != nullptr ? stage_->PlayedStage() : nullptr;
    }
    // The stage is live play, which follows the Aspect Ratio setting.
    bool WidensDisplay() const {
        const Level::Stage* stage = PlayedStage();
        return stage_ != nullptr && !(stage != nullptr && stage->FixedAspect());
    }
    // The size of the screen the stage draws on this frame.
    std::pair<int32_t, int32_t> ScreenSize() const {
        if (const Level::Stage* stage = PlayedStage()) {
            return { stage->ScreenWidth(), stage->ScreenHeight() };
        }
        return { 320, 240 };
    }
    // init_menu0B_runstage(): loads the stage select_ramrom_to_play picked, or
    // the stage the menus selected.
    void Init();
    // The demo's frame, then back to the front-end once it has faded out; or a
    // tick of the stage the player is in, then the mission's end screens once
    // it has ended.
    void Interface(MenuStateMachine& menu);
    Gfx* Render(Gfx* dl);
    // Lines the picture is presented shifted down this frame; valid after Render().
    int32_t ScreenShakeOffset() const {
        if (stage_) {
            return stage_->ScreenShakeOffset();
        }
        return run_ != nullptr ? run_->ScreenShakeOffset() : 0;
    }
    void WaitForFrame() const;

  private:
    std::shared_ptr<Ship::ResourceManager> resourceManager_;
    std::shared_ptr<FrontState> front_;
    DemoRunOptions options_;
    std::unique_ptr<DemoRun> run_;
    StageRunFactory startStage_;
    std::unique_ptr<StageRun> stage_;
    // A stage the menus selected failed to load.
    bool stageFailed_ = false;
};

// Registers MenuId::RunStage's handlers (init_menu0B_runstage() and the
// return to the front-end the demo's fade to the title leads to).
void RegisterRunStage(MenuStateMachine& menu, const std::shared_ptr<RunStageHost>& host);

// select_ramrom_to_play(): picks one of the unlocked demos at random and
// switches to MenuId::RunStage to play it. Returns false when there are no
// demos to play.
bool SelectRamromToPlay(MenuStateMachine& menu, FrontState& front,
                        const std::shared_ptr<Ship::ResourceManager>& resourceManager);

} // namespace Frigate::Game::Menu
