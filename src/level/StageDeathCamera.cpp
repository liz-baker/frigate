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

bool SameVec(const Vec3& a, const Vec3& b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

} // namespace

void Stage::EnterDeathCamera() {
    // bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP).
    cameraMode_ = CameraMode::DeathCamSinglePlayer;
    deathCamera_.advance = false;
    deathCamera_.timer = 0.0f;
    colour_.SetColour(0, 0, 0, 1.0f);
    colour_.SetFrac(kFadeTicks, 0.0f);

    Stan& stan = level_.StanFile();
    const bool tank = player_.ExplodeTankOnDeath() && player_.PlayerTankProp() >= 0;
    deathCamera_.tank = tank;
    if (!tank) {
        player_.RestoreDeathState(stan);
        colour_.StartBodyFade(0.0f, 1.0f, bodyFadeAlpha_);
        // TODO(https://github.com/liz-baker/frigate/issues/140): solo_char_load
        // and the body's death animation (ACT_BONDDIE) that this replay plays, on
        // Bond's third-person model.
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
    deathCamera_.advance = false;
    colour_.SetColour(0, 0, 0, 0.0f);
    colour_.SetFrac(kFadeTicks, 1.0f);
}

void Stage::AdvanceCameraMode() {
    // bondviewAdvanceCameraMode. Only the death cameras raise
    // g_CameraAfterCinema yet; the intro cameras' steps belong with them.
    // TODO(https://github.com/liz-baker/frigate/issues/140): those steps.
    const CameraMode mode = cameraMode_;
    cameraMode_ = CameraMode::None;
    deathCamera_.advance = false;

    if (mode == CameraMode::DeathCamSinglePlayer) {
        EnterDeathCameraFade();
    } else if (mode == CameraMode::DeathCamMultiPlayer) {
        deathCamera_.replays++;
        if (deathCamera_.replays < kDeathReplays) {
            EnterDeathCamera();
        }
    }
}

void Stage::TickDeathCamera(uint16_t newButtons) {
    if (!DeathCameraShowing()) {
        return;
    }
    // bondviewFrozenCameraTick.
    deathCamera_.timer += clock_.globalTimerDelta;

    const bool skipped = (newButtons & kSkipDeathButtons) != 0 && health_.IsDead() && health_.RedBloodFinished() &&
                         health_.DeathAnimFinished();
    if (cameraMode_ == CameraMode::DeathCamSinglePlayer) {
        if (deathCamera_.tank) {
            // The tank has no body to replay.
            if (deathCamera_.timer >= kTankReplayTicks) {
                deathCamera_.advance = true;
            }
        } else if (colour_.IsComplete()) {
            // TODO(https://github.com/liz-baker/frigate/issues/140): the replay
            // ends when the body's death animation does; without the body it
            // ends once the fade in has finished.
            deathCamera_.advance = true;
        }
    } else if (colour_.IsComplete()) {
        deathCamera_.advance = true;
    }
    if (skipped) {
        if (cameraMode_ == CameraMode::DeathCamSinglePlayer) {
            deathCamera_.advance = true;
        }
        deathCamera_.replays = kSkippedReplays;
    }

    const Vec3 lookAt = deathCamera_.lookProp == player_.PropId() ? player_.SmoothedPosition()
                                                                  : objects_.Props()[deathCamera_.lookProp].position;
    PlaceCamera(deathCamera_.spot, lookAt, player_.Tile(), player_.CollisionPosition());
}

void Stage::PlaceCamera(const Vec3& position, const Vec3& lookAt, int tile, const Vec3& walkStart) {
    // bondviewSetCurrentPlayerPosition, with g_DefaultFrozenPlayerOffset as the
    // up vector.
    const Vec3 offset = { 0.0f, 1.0f, 0.0f };
    DeathCameraState& camera = deathCamera_;
    if (SameVec(position, camera.position) && SameVec(lookAt, camera.lookAt) && SameVec(offset, camera.offset) &&
        camera.tile != kNoTile) {
        return;
    }

    Stan& stan = level_.StanFile();
    int fromStart = tile;
    if (stan.WalkTiles(&fromStart, walkStart[0], walkStart[2], position[0], position[2])) {
        camera.tile = fromStart;
    } else if (camera.tile != kNoTile) {
        int fromPrevious = camera.tile;
        camera.tile = stan.WalkTiles(&fromPrevious, camera.position[0], camera.position[2], position[0], position[2])
                          ? fromPrevious
                          : fromStart;
    } else {
        camera.tile = fromStart;
    }

    camera.position = position;
    camera.lookAt = lookAt;
    camera.offset = offset;
    camera.floor = { position[0], stan.PositionY(camera.tile, position[0], position[2]), position[2] };
}

PlayerPose Stage::CameraPose(const PlayerPose& pose) const {
    // cameramode 1: the view looks from pos to pos2 with offset up.
    const DeathCameraState& camera = deathCamera_;
    if (!DeathCameraShowing() || camera.tile == kNoTile) {
        return pose;
    }
    PlayerPose placed = pose;
    placed.position = camera.position;
    placed.previousPosition = camera.floor;
    placed.look = { camera.lookAt[0] - camera.position[0], camera.lookAt[1] - camera.position[1],
                    camera.lookAt[2] - camera.position[2] };
    placed.up = camera.offset;
    placed.room = level_.StanFile().TileRoom(camera.tile);
    placed.cameraMode = cameraMode_;
    return placed;
}

} // namespace Frigate::Level
