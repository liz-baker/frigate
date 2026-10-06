#pragma once

#include <functional>
#include <memory>

#include "level/PlayerCamera.h"
#include "level/PlayerMove.h"
#include "level/ResourceBgSource.h"
#include "level/Stage.h"
#include "level/StageResult.h"

union Gfx;

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {

struct FrontState;

namespace Menu {
struct SelectedStage;
}

// A stage the player is playing, as RunStageHost drives it.
class StageRun {
  public:
    virtual ~StageRun() = default;

    // One tick of the stage with the controller's reading, in the frame order
    // of lvlTick: the clock and pressed buttons, the stage's tick, then the
    // player's move.
    virtual void Step(const Level::MoveInput& input) = 0;
    // The stage has called bossReturnTitleStage or bossRunTitleStage.
    virtual bool Done() const = 0;
    // What the stage left for the mission screens; valid once Done().
    virtual Level::StageResult Result() const = 0;
    virtual Gfx* Render(Gfx* dl) = 0;
    // Lines the picture is presented shifted down this frame; valid after Render().
    virtual int32_t ScreenShakeOffset() const {
        return 0;
    }
};

// Loads the stage the menus selected.
using StageRunFactory = std::function<std::unique_ptr<StageRun>(const Menu::SelectedStage& selected)>;

// A selected stage played from its start with the first controller.
class LiveRun final : public StageRun {
  public:
    // lvlStageLoad for the selected stage and difficulty, with the folder's
    // settings (already applied to front.settings) as the player's options
    // and the watch saving them back into the folder. Null when the stage
    // can't be loaded.
    static std::unique_ptr<LiveRun> Start(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                          const Menu::SelectedStage& selected, FrontState& front);
    ~LiveRun() override;

    void Step(const Level::MoveInput& input) override;
    bool Done() const override {
        return stage_->Exit() != Level::StageExit::None;
    }
    Level::StageResult Result() const override {
        return stage_->Result();
    }
    Gfx* Render(Gfx* dl) override;
    int32_t ScreenShakeOffset() const override {
        return stage_->ScreenShakeOffset();
    }

  private:
    explicit LiveRun(const std::shared_ptr<Ship::ResourceManager>& resourceManager);

    std::shared_ptr<Ship::ResourceManager> resourceManager_;
    std::unique_ptr<Level::ResourceBgSource> source_;
    std::unique_ptr<Level::Stage> stage_;
    Level::PlayerPose pose_;
};

} // namespace Frigate::Game
