#include "level/Stage.h"

namespace Frigate::Level {

namespace {

// CHRAI_TICKRATE_F.
constexpr float kFadeTicks = 60.0f;

// The buttons that carry on once the mission is over.
constexpr uint16_t kContinueButtons = Buttons::A | Buttons::B | Buttons::Z | Buttons::Start | Buttons::R | Buttons::L;

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
    const uint16_t newButtons = input.buttons & static_cast<uint16_t>(~Cur().previousButtons);

    // stop_time_flag: once the mission is over, a button fades the view out
    // and the stage returns to the title.
    if (stopTime_ != 0) {
        if (!controlsLocked_ && (newButtons & kContinueButtons) != 0) {
            stopTime_ = 2;
            if (Cur().colour.IsComplete()) {
                if (Cur().colour.Frac() == 0.0f) {
                    Cur().colour.SetColour(0, 0, 0, 0.0f);
                    Cur().colour.SetFrac(kFadeTicks, 1.0f);
                }
            } else if (Cur().colour.FracNew() == 0.0f) {
                Cur().colour.SetFrac(Cur().colour.FadeTime(), 1.0f);
            }
        }
        if (stopTime_ == 2 && Cur().colour.IsComplete() && Cur().colour.Frac() == 1.0f) {
            ReturnTitleStage();
        }
    }

    // g_CameraAfterCinema.
    if (cameraAfterCinema_ != CameraMode::None) {
        AdvanceCameraMode();
    }

    if (Cur().health.IsDead()) {
        if (!Cur().health.RedBloodFinished() && Cur().weapons.Ready()) {
            Cur().weapons.EquipWeapon(HandLeft, kItemUnarmed);
            Cur().weapons.EquipWeapon(HandRight, kItemUnarmed);
        }
        // The death camera's replays are over.
        if (Cur().health.RedBloodFinished() && Cur().health.DeathAnimFinished() && deathCamera_.replays >= kDeathReplays) {
            RunTitleStage();
        }
    }

    Cur().previousButtons = input.buttons;
}

StageResult Stage::Result() const {
    const ObjectiveWorld world = MakeObjectiveWorld();
    StageResult result;
    result.exit = exit_;
    result.missionTimer = missionTimer_;
    result.killCount = CurStats().killCount;
    for (size_t i = 0; i < result.shots.size(); i++) {
        result.shots[i] = CurStats().shotCount[i];
    }
    // set_favorite_weapon_for_every_player, which the stage's unload runs.
    result.favouriteWeapon = Cur().weapons.Ready() ? Cur().weapons.WeaponOfChoice() : std::array<int32_t, 2>{};
    result.objectives = objectives_.StatusesOnExit(world);
    result.objectivesCompleteAtExit = objectives_.AllComplete(world);
    result.bondKilledInAction = bondKilledInAction_;
    result.aborted = aborted_;
    return result;
}

} // namespace Frigate::Level
