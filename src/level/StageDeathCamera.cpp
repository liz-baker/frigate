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
    colour_.SetColour(0, 0, 0, 1.0f);
    colour_.SetFrac(kFadeTicks, 0.0f);

    Stan& stan = level_.StanFile();
    const bool tank = player_.ExplodeTankOnDeath() && player_.PlayerTankProp() >= 0;
    if (!tank) {
        player_.RestoreDeathState(stan);
        StartBodyFade(0.0f, 1.0f);
        LoadBondBody();
        if (bondChr_ >= 0) {
            StartBondDeathAnimation();
        }
    }

    PropList& props = objects_.Props();
    DeathCameraRequest request;
    int subject;
    if (tank) {
        subject = player_.PlayerTankProp();
        const Prop& prop = props[subject];
        request.lookAt = prop.position;
        request.collisionPosition = prop.position;
        request.tile = prop.stan;
        request.distance = kTankDistance;
    } else {
        subject = player_.PropId();
        request.lookAt = player_.SmoothedPosition();
        request.collisionPosition = player_.CollisionPosition();
        request.tile = player_.Tile();
        request.distance = kPlayerDistance;
    }
    request.clearance = player_.CollisionRadiusField();
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
    colour_.SetColour(0, 0, 0, 0.0f);
    colour_.SetFrac(kFadeTicks, 1.0f);
}

Stage::FrozenCamera Stage::DeathCameraTick(uint16_t newButtons) {
    // bondviewFrozenCameraTick.
    cameraTransitionTimer_ += clock_.globalTimerDelta;

    const bool skipped = (newButtons & kSkipDeathButtons) != 0 && health_.IsDead() && health_.RedBloodFinished() &&
                         health_.DeathAnimFinished();
    if (cameraMode_ == CameraMode::DeathCamSinglePlayer) {
        if (bondChr_ >= 0) {
            // The replay ends when the body's death animation does.
            const ModelPlayback& playback = chrs_[bondChr_].model->Playback();
            if (playback.frame >= playback.AnimEndFrame()) {
                cameraAfterCinema_ = cameraMode_;
            }
        } else if (cameraTransitionTimer_ >= kTankReplayTicks) {
            // The tank has no body to replay.
            cameraAfterCinema_ = cameraMode_;
        }
    } else if (colour_.IsComplete()) {
        cameraAfterCinema_ = cameraMode_;
    }
    if (skipped) {
        if (cameraMode_ == CameraMode::DeathCamSinglePlayer) {
            cameraAfterCinema_ = cameraMode_;
        }
        deathCamera_.replays = kSkippedReplays;
    }

    const Vec3 lookAt = deathCamera_.lookProp == player_.PropId() ? player_.SmoothedPosition()
                                                                  : objects_.Props()[deathCamera_.lookProp].position;
    // bondviewSetCurrentPlayerPosition, with g_DefaultFrozenPlayerOffset as the
    // up vector.
    FrozenCamera camera;
    camera.position = deathCamera_.spot;
    camera.lookAt = lookAt;
    camera.tile = player_.Tile();
    camera.walkStart = player_.CollisionPosition();
    return camera;
}

} // namespace Frigate::Level
