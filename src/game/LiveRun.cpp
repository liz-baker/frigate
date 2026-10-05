#include "game/LiveRun.h"

#include <spdlog/spdlog.h>

#include "game/DemoRun.h"
#include "game/FrontState.h"
#include "game/menu/StageStart.h"
#include "game/ControlStyleOverride.h"
#include "game/save/GameSettings.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game {

LiveRun::LiveRun(const std::shared_ptr<Ship::ResourceManager>& resourceManager) : resourceManager_(resourceManager) {
}

LiveRun::~LiveRun() {
    if (stage_) {
        stage_->Unload();
    }
}

std::unique_ptr<LiveRun> LiveRun::Start(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                        const Menu::SelectedStage& selected, FrontState& front) {
    std::unique_ptr<LiveRun> run(new LiveRun(resourceManager));
    run->source_ = std::make_unique<Level::ResourceBgSource>(resourceManager);
    run->stage_ = std::make_unique<Level::Stage>(*run->source_, run->source_.get());
    Level::Stage& stage = *run->stage_;
    if (!stage.Load(static_cast<Level::LevelId>(selected.stage), /*demoSlot=*/0, selected.difficulty)) {
        spdlog::error("run stage: {} (the archives may predate this build; re-run `just pack_archives`)",
                      stage.Error());
        run->stage_.reset();
        return nullptr;
    }
    // fileLoadSettingsForFolder's options, and deleteCurrentSelectedFolder
    // saving the watch's changes to them back into the folder.
    stage.Options() = front.settings->options;
    stage.SetSettingsSaver(Menu::SettingsSaver(front, stage.Options()));

    run->pose_ = StagePlayerStartPose(stage);
    // TODO(https://github.com/liz-baker/frigate/issues/140): the stage opens
    // on the intro camera's swirl; play starts in first person at the spawn.
    stage.EnterSwirl();
    stage.EnterFirstPerson();
    run->pose_.cameraMode = Level::CameraMode::FirstPerson;
    return run;
}

void LiveRun::Step(const Level::MoveInput& input) {
    Level::Stage& stage = *stage_;
    const int style = ControlStyleOverride();
    stage.Options().controlOverride =
        style >= 0 ? std::optional(static_cast<Level::ControlConfig>(style)) : std::nullopt;
    // The frame order of a demo run: the clock, the pressed buttons, the
    // stage's tick, then the player's move. The simulation steps one tick per
    // displayed frame.
    stage.SetClockTimer(1);
    stage.SetButtonsPressed(input.pressed);
    stage.Tick();
    stage.MovePlayer(input);
    pose_ = PlayerPoseOf(stage.Player());
    pose_.cameraMode = Level::CameraMode::FirstPerson;
}

Gfx* LiveRun::Render(Gfx* dl) {
    dl = ClearStageFrame(dl, /*magenta=*/false);
    return stage_->Render(dl, resourceManager_, pose_);
}

} // namespace Frigate::Game
