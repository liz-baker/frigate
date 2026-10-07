#include "game/LiveRun.h"

#include <spdlog/spdlog.h>

#include <vector>

#include "game/DemoRun.h"
#include "game/FrontState.h"
#include "game/menu/StageStart.h"
#include "game/ControlStyleOverride.h"
#include "game/save/GameSettings.h"
#include "gfx/DisplayAspect.h"
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
    stage.SetSliders007(front.sliders007);
    const bool multiplayer = front.gameMode == GameMode::Multi;
    if (multiplayer) {
        const char* reason = nullptr;
        if (!front.multiplayer.Valid(&reason)) {
            spdlog::error("run stage: the multiplayer settings are not playable: {}", reason);
            return nullptr;
        }
        stage.SetMultiplayer(front.multiplayer);
        stage.SetFavouriteWeapons(&front.favouriteWeapons);
    }
    // lvlStageLoad clears g_NewCheatUnlocked; the cheats switch on only when
    // the mission was picked from the cheat variant of the mission screen.
    front.newCheatUnlocked = false;
    if (front.appendCheatSinglePlayer) {
        std::vector<int32_t> cheats;
        for (size_t cheat = 1; cheat < kCheatInvalid; cheat++) {
            if (front.cheatActivated[cheat]) {
                cheats.push_back(static_cast<int32_t>(cheat));
            }
        }
        stage.SetCheats(std::move(cheats));
    }
    if (!stage.Load(static_cast<Level::LevelId>(selected.stage), /*demoSlot=*/0, selected.difficulty, /*demo=*/false)) {
        spdlog::error("run stage: {} (the archives may predate this build; re-run `just pack_archives`)",
                      stage.Error());
        run->stage_.reset();
        return nullptr;
    }
    if (!multiplayer) {
        // fileLoadSettingsForFolder's options, and deleteCurrentSelectedFolder
        // saving the watch's changes to them back into the folder.
        stage.Options() = front.settings->options;
        stage.SetSettingsSaver(Menu::SettingsSaver(front, stage.Options()));
    }

    run->pose_ = StagePlayerStartPose(stage);
    if (!multiplayer) {
        // A multiplayer stage opens on its own camera mode, which Load set.
        stage.EnterIntro();
    }
    run->pose_.cameraMode = stage.CameraPose().cameraMode;
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
    stage.SetDisplayWiden(Display::DisplayWiden());
    stage.SetClockTimer(1);
    stage.SetButtonsPressed(input.pressed);
    stage.Tick();
    stage.MovePlayer(input);
    pose_ = stage.CameraPose();
}

void LiveRun::StepPads(const Platform::Input::State& pads, int controllers) {
    Level::Stage& stage = *stage_;
    const int style = ControlStyleOverride();
    for (int player = 0; player < stage.PlayerCount(); player++) {
        stage.PlayerAt(player).options.controlOverride =
            style >= 0 ? std::optional(static_cast<Level::ControlConfig>(style)) : std::nullopt;
    }
    stage.SetDisplayWiden(Display::DisplayWiden());
    stage.TickLive(pads, controllers);
    pose_ = stage.CameraPose();
}

Gfx* LiveRun::Render(Gfx* dl) {
    dl = ClearStageFrame(dl);
    if (stage_->PlayerCount() > 1) {
        return stage_->Render(dl, resourceManager_);
    }
    return stage_->Render(dl, resourceManager_, pose_);
}

} // namespace Frigate::Game
