#include "level/Stage.h"

namespace Frigate::Level {

namespace {

// CHRAI_TICKRATE_F.
constexpr float kFadeTicks = 60.0f;

// The buttons that carry on once the mission is over, and out of the death
// screen.
constexpr uint16_t kContinueButtons = Buttons::A | Buttons::B | Buttons::Z | Buttons::Start | Buttons::R | Buttons::L;
constexpr uint16_t kSkipDeathButtons = Buttons::A | Buttons::B | Buttons::Z | Buttons::Start;

} // namespace

void Stage::ReturnTitleStage() {
    // bossReturnTitleStage.
    if (exit_ == StageExit::None) {
        exit_ = StageExit::ReturnTitle;
    }
}

void Stage::RunTitleStage() {
    // bossRunTitleStage.
    if (exit_ == StageExit::None) {
        exit_ = StageExit::RunTitle;
    }
}

void Stage::TickExits(const MoveInput& input) {
    const uint16_t newButtons = input.buttons & static_cast<uint16_t>(~previousButtons_);

    // stop_time_flag: once the mission is over, a button fades the view out
    // and the stage returns to the title.
    if (stopTime_ != 0) {
        if (!controlsLocked_ && (newButtons & kContinueButtons) != 0) {
            stopTime_ = 2;
            if (colour_.IsComplete()) {
                if (colour_.Frac() == 0.0f) {
                    colour_.SetColour(0, 0, 0, 0.0f);
                    colour_.SetFrac(kFadeTicks, 1.0f);
                }
            } else if (colour_.FracNew() == 0.0f) {
                colour_.SetFrac(colour_.FadeTime(), 1.0f);
            }
        }
        if (stopTime_ == 2 && colour_.IsComplete() && colour_.Frac() == 1.0f) {
            ReturnTitleStage();
        }
    }

    // A dead player leaves once the death animation has faded out and a
    // button is pressed (the death camera's skip, CAMERAMODE_DEATH_CAM_SP).
    // TODO(https://github.com/liz-baker/frigate/issues/136): the death camera
    // replays, which end the stage on their own after the third.
    if (health_.IsDead() && health_.RedBloodFinished() && health_.DeathAnimFinished() && colour_.IsComplete() &&
        (newButtons & kSkipDeathButtons) != 0) {
        RunTitleStage();
    }

    previousButtons_ = input.buttons;
}

StageResult Stage::Result() const {
    const ObjectiveWorld world = MakeObjectiveWorld();
    StageResult result;
    result.exit = exit_;
    result.missionTimer = missionTimer_;
    result.killCount = stats_[0].killCount;
    for (size_t i = 0; i < result.shots.size(); i++) {
        result.shots[i] = stats_[0].shotCount[i];
    }
    // set_favorite_weapon_for_every_player, which the stage's unload runs.
    result.favouriteWeapon = weapons_.Ready() ? weapons_.WeaponOfChoice() : std::array<int32_t, 2>{};
    result.objectives = objectives_.StatusesOnExit(world);
    result.objectivesCompleteAtExit = objectives_.AllComplete(world);
    result.bondKilledInAction = bondKilledInAction_;
    result.aborted = aborted_;
    return result;
}

} // namespace Frigate::Level
