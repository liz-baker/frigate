#include "game/Sound.h"
#include "level/DeathCamera.h"
#include "level/Stage.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// CHRAI_TICKRATE_F.
constexpr float kFadeTicks = 60.0f;
// How far the camera stands from the player and from a tank, and the time the
// tank's replay lasts (camera_transition_timer).
constexpr float kPlayerDistance = 200.0f;
constexpr float kTankDistance = 500.0f;
constexpr float kTankReplayTicks = 180.0f;
// EXPLOSION_DEF_STANDARD.
constexpr int kExplosionStandard = 13;
// CAMERAMODE_FADESWIRL, which a button sets the replay count to.
constexpr int kSkippedReplays = 2;
constexpr uint16_t kSkipDeathButtons = Buttons::A | Buttons::B | Buttons::Z | Buttons::Start;

} // namespace

void Stage::EnterDeathCamera() {
    // bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP).
    cameraMode_ = CameraMode::DeathCamSinglePlayer;
    cameraAfterCinema_ = CameraMode::None;
    cameraTransitionTimer_ = 0.0f;
    Cur().colour.SetColour(0, 0, 0, 1.0f);
    Cur().colour.SetFrac(kFadeTicks, 0.0f);

    Stan& stan = level_.StanFile();
    const bool tank = Cur().move.ExplodeTankOnDeath() && Cur().move.PlayerTankProp() >= 0;
    if (!tank) {
        Cur().move.RestoreDeathState(stan);
        StartBodyFade(0.0f, 1.0f);
        LoadBondBody();
        if (Cur().bondChr >= 0) {
            StartBondDeathAnimation();
        }
    }

    PropList& props = objects_.Props();
    DeathCameraRequest request;
    int subject;
    if (tank) {
        subject = Cur().move.PlayerTankProp();
        const Prop& prop = props[subject];
        request.lookAt = prop.position;
        request.collisionPosition = prop.position;
        request.tile = prop.stan;
        request.distance = kTankDistance;
    } else {
        subject = Cur().move.PropId();
        request.lookAt = Cur().move.SmoothedPosition();
        request.collisionPosition = Cur().move.CollisionPosition();
        request.tile = Cur().move.Tile();
        request.distance = kPlayerDistance;
    }
    request.clearance = Cur().move.CollisionRadiusField();
    request.setSubjectCollides = [this, subject](bool collides) { SetPropCollides(subject, collides); };
    deathCamera_.lookProp = subject;

    if (PickDeathCameraAngles(stan, request, RandomGetNext, &deathCamera_.spot)) {
        if (deathCamera_.replays == 0) {
            Game::Sound::MusicTrackPlay(0, AudioDriver::kMusicIntroSwoosh);
            Game::Sound::SetScalerApplyVolumeAllSfxSlot(0.5f);
        }
        if (tank) {
            const Prop& prop = props[subject];
            explosions_.Create(subject, prop.position, prop.stan, kExplosionStandard, 0, 0, prop.rooms, false,
                               RandomGetNext);
        }
    } else {
        // No bearing leaves room for the camera.
        RunTitleStage();
    }
}

void Stage::EnterDeathCameraFade() {
    // bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_MP): the fade to black that
    // ends a replay.
    cameraMode_ = CameraMode::DeathCamMultiPlayer;
    cameraAfterCinema_ = CameraMode::None;
    Cur().colour.SetColour(0, 0, 0, 0.0f);
    Cur().colour.SetFrac(kFadeTicks, 1.0f);
}

Stage::FrozenCamera Stage::DeathCameraTick(uint16_t newButtons) {
    // bondviewFrozenCameraTick.
    cameraTransitionTimer_ += clock_.globalTimerDelta;

    const bool skipped = (newButtons & kSkipDeathButtons) != 0 && Cur().health.IsDead() &&
                         Cur().health.RedBloodFinished() && Cur().health.DeathAnimFinished();
    if (cameraMode_ == CameraMode::DeathCamSinglePlayer) {
        if (Cur().bondChr >= 0) {
            // The replay ends when the body's death animation does.
            const ModelPlayback& playback = chrs_[Cur().bondChr].model->Playback();
            if (playback.frame >= playback.AnimEndFrame()) {
                cameraAfterCinema_ = cameraMode_;
            }
        } else if (cameraTransitionTimer_ >= kTankReplayTicks) {
            // The tank has no body to replay.
            cameraAfterCinema_ = cameraMode_;
        }
    } else if (Cur().colour.IsComplete()) {
        cameraAfterCinema_ = cameraMode_;
    }
    if (skipped) {
        if (cameraMode_ == CameraMode::DeathCamSinglePlayer) {
            cameraAfterCinema_ = cameraMode_;
        }
        deathCamera_.replays = kSkippedReplays;
    }

    const Vec3 lookAt = deathCamera_.lookProp == Cur().move.PropId() ? Cur().move.SmoothedPosition()
                                                                     : objects_.Props()[deathCamera_.lookProp].position;
    // bondviewSetCurrentPlayerPosition, with g_DefaultFrozenPlayerOffset as the
    // up vector.
    FrozenCamera camera;
    camera.position = deathCamera_.spot;
    camera.lookAt = lookAt;
    camera.tile = Cur().move.Tile();
    camera.walkStart = Cur().move.CollisionPosition();
    return camera;
}

} // namespace Frigate::Level
