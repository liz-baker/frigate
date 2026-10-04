#include "level/Stage.h"

#include <cmath>

#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// CHRAI_TICKRATE_F: the length of the camera's fades.
constexpr float kFadeTicks = 60.0f;
// The buttons that skip the establishing shot and the swirl.
constexpr uint16_t kSkipButtons = Buttons::A | Buttons::B | Buttons::Z | Buttons::Start | Buttons::R | Buttons::L;
// How long the establishing shot waits before its first caption, then its
// second, and how long it holds before moving on with one caption and with two.
constexpr float kFirstCaptionTime = 120.0f;
constexpr float kSecondCaptionTime = 300.0f;
constexpr float kEndWithOneCaption = 300.0f;
constexpr float kEndWithTwoCaptions = 480.0f;
// The swirl fades Bond out when this many ticks of it remain, and takes a
// button press to skip it while more than kSwirlSkipTime remain.
constexpr float kBodyFadeTime = 30.0f;
constexpr float kSwirlSkipTime = 60.0f;

} // namespace

void Stage::LoadFogEnvironment(bool cinema) {
    fog_.LoadLevelEnvironment(currentStage_, cinema, kPlayerCount, level_.VisibilityScale());
    camera_.SetZRange(fog_.ZNear(), fog_.ZFar());
}

void Stage::EnterIntro() {
    SetCameraMode(CameraMode::Intro);
}

void Stage::StartBodyFade(float duration, float targetFrac) {
    if (bondChr_ < 0) {
        return;
    }
    colour_.StartBodyFade(duration, targetFrac, chrs_[bondChr_].fadeAlpha);
}

void Stage::SetCameraMode(CameraMode mode) {
    cameraMode_ = mode;
    cameraAfterCinema_ = CameraMode::None;

    // Neither a demo being recorded nor played shows the intro camera or the
    // swirl; recording is not something this port does.
    switch (mode) {
        case CameraMode::Intro:
            if (intro_.selectedCamera && !demo_) {
                cameraTransitionTimer_ = 0.0f;
                colour_.SetColour(0, 0, 0, 1.0f);
                colour_.SetFrac(kFadeTicks, 0.0f);
                LoadFogEnvironment(/*cinema=*/true);
                cameraTile_ = kNoTile;
            } else {
                SetCameraMode(CameraMode::Swirl);
            }
            break;
        case CameraMode::FadeSwirl:
            colour_.SetColour(0, 0, 0, 0.0f);
            colour_.SetFrac(kFadeTicks, 1.0f);
            break;
        case CameraMode::Swirl:
            cameraFadeActive_ = false;
            colour_.SetColour(0, 0, 0, 1.0f);
            colour_.SetFrac(kFadeTicks, 0.0f);
            LoadFogEnvironment(/*cinema=*/false);
            if (!intro_.swirls.empty() && !demo_) {
                cameraTransitionTimer_ = 0.0f;
                // intro_camera_index starts at 1, the record after the first
                // control point.
                introCameraIndex_ = 1;
                StartBodyFade(0.0f, 1.0f);
                cameraTile_ = kNoTile;
            } else {
                SetCameraMode(CameraMode::FirstPerson);
            }
            break;
        case CameraMode::FirstPerson:
            if (currentStage_ == LevelId::Cuba) {
                colour_.SetColour(0, 0, 0, 1.0f);
                colour_.SetFrac(0.0f, 1.0f);
            } else if (cameraFadeActive_) {
                colour_.SetColour(0, 0, 0, 1.0f);
                colour_.SetFrac(kFadeTicks, 0.0f);
            }
            if (weapons_.Ready() && watch_.State() == WatchState::Closed) {
                weapons_.EquipWeapon(HandLeft, intro_.startingWeaponLeft);
                weapons_.EquipWeapon(HandRight, intro_.startingWeaponRight);
            }
            stopTime_ = 0;
            break;
        default:
            break;
    }
}

void Stage::AdvanceCameraMode() {
    const CameraMode mode = cameraMode_;
    cameraMode_ = CameraMode::None;
    cameraAfterCinema_ = CameraMode::None;

    switch (mode) {
        case CameraMode::Intro:
            SetCameraMode(CameraMode::FadeSwirl);
            break;
        case CameraMode::FadeSwirl:
            hud_.ResetIntroCameraMessages();
            SetCameraMode(CameraMode::Swirl);
            break;
        case CameraMode::Swirl:
            StartBodyFade(0.0f, 1.0f);
            SetCameraMode(CameraMode::FirstPerson);
            break;
        case CameraMode::DeathCamSinglePlayer:
            EnterDeathCameraFade();
            break;
        case CameraMode::DeathCamMultiPlayer:
            deathCamera_.replays++;
            if (deathCamera_.replays < kDeathReplays) {
                EnterDeathCamera();
            }
            break;
        default:
            break;
    }
}

Stage::FrozenCamera Stage::FrozenCameraTick(const MoveInput& input) {
    FrozenCamera camera;
    const float delta = clock_.globalTimerDelta;
    const uint16_t newButtons = input.buttons & static_cast<uint16_t>(~previousButtons_);

    // A pad the game reads the tile of unchecked; one that found none takes the
    // player's tile.
    const auto padTile = [&](const Pad& pad) { return pad.stanTile != kNoTile ? pad.stanTile : player_.Tile(); };

    if (DeathCameraShowing()) {
        return DeathCameraTick(newButtons);
    }

    if (cameraMode_ == CameraMode::Intro || cameraMode_ == CameraMode::FadeSwirl) {
        const IntroCamera& shot = intro_.cameras[*intro_.selectedCamera];
        if (cameraMode_ == CameraMode::Intro) {
            const float time = cameraTransitionTimer_;
            if (time < kFirstCaptionTime && time + delta >= kFirstCaptionTime) {
                hud_.SetMessageFont(HudFont::Zurich);
                hud_.BottomShow(lang_.Get(shot.caption[1]));
            }
            if (shot.secondCaption != 0) {
                if (time < kSecondCaptionTime && time + delta >= kSecondCaptionTime) {
                    hud_.BottomShow(lang_.Get(static_cast<uint16_t>(shot.secondCaption)));
                }
                if (time > kEndWithTwoCaptions) {
                    cameraAfterCinema_ = CameraMode::Intro;
                }
            } else if (time > kEndWithOneCaption) {
                cameraAfterCinema_ = CameraMode::Intro;
            }

            cameraTransitionTimer_ += delta;

            if (!controlsLocked_ && (newButtons & kSkipButtons) != 0) {
                cameraAfterCinema_ = CameraMode::Intro;
            }
        } else if (colour_.IsComplete()) {
            cameraAfterCinema_ = CameraMode::Intro;
        }

        camera.position = shot.position;
        camera.lookAt = { camera.position[0] + (GameCosf(shot.pitch) * GameSinf(shot.yaw)),
                          camera.position[1] + GameSinf(shot.pitch),
                          camera.position[2] - (GameCosf(shot.pitch) * GameCosf(shot.yaw)) };
        const Pad& pad = setup_->Pads()[static_cast<size_t>(shot.pad)];
        camera.tile = padTile(pad);
        camera.walkStart = pad.position;
        return camera;
    }

    // CAMERAMODE_SWIRL.
    const std::vector<IntroSwirl>& swirls = intro_.swirls;
    // The records past the last are read unchecked; a swirl ends with a record
    // that starts a path.
    const auto flagsAt = [&](size_t index) {
        return index < swirls.size() ? swirls[index].flags : SwirlFlag::kPathStart;
    };
    const auto durationAt = [&](size_t index) { return index < swirls.size() ? swirls[index].duration : 0.0f; };

    cameraTransitionTimer_ += delta;
    while (durationAt(introCameraIndex_) <= cameraTransitionTimer_) {
        if ((flagsAt(introCameraIndex_ + 3) & SwirlFlag::kPathStart) == 0) {
            cameraTransitionTimer_ -= durationAt(introCameraIndex_);
            introCameraIndex_++;
        } else {
            cameraTransitionTimer_ = durationAt(introCameraIndex_);
            cameraAfterCinema_ = CameraMode::Intro;
            break;
        }
    }

    // The ticks the swirl has left.
    float remaining = durationAt(introCameraIndex_) - cameraTransitionTimer_;
    for (size_t i = introCameraIndex_ + 1; (flagsAt(i + 2) & SwirlFlag::kPathStart) == 0; i++) {
        remaining += durationAt(i);
    }

    // Bond fades out just before the player takes control.
    if (remaining < kBodyFadeTime && remaining + delta >= kBodyFadeTime) {
        StartBodyFade(kBodyFadeTime, 0.0f);
    }

    if (cameraFadeActive_) {
        if (colour_.IsComplete()) {
            cameraAfterCinema_ = CameraMode::Intro;
        }
    }

    if (remaining > kSwirlSkipTime && !cameraFadeActive_) {
        if (!controlsLocked_ && (newButtons & kSkipButtons) != 0) {
            cameraFadeActive_ = true;
            colour_.SetColour(0, 0, 0, colour_.Frac());
            if (colour_.IsComplete()) {
                colour_.SetFrac(kFadeTicks, 1.0f);
            } else {
                colour_.SetFrac(colour_.FadeTime(), 1.0f);
            }
        }
    }

    const SwirlCamera swirl = CalcIntroSwirlCamera(swirls, introCameraIndex_, cameraTransitionTimer_,
                                                   player_.SmoothedPosition(), player_.LookDirection(),
                                                   player_.AppliedView());
    camera.position = swirl.position;
    camera.lookAt = swirl.lookAt;

    const int32_t swirlPad = introCameraIndex_ < swirls.size() ? swirls[introCameraIndex_].pad : -1;
    if (swirlPad >= 0) {
        const Pad& pad = setup_->Pads()[static_cast<size_t>(swirlPad)];
        camera.tile = padTile(pad);
        camera.walkStart = pad.position;
    } else {
        camera.tile = player_.Tile();
        camera.walkStart = player_.CollisionPosition();
    }
    return camera;
}

void Stage::SetFrozenCamera(const FrozenCamera& camera) {
    Stan& stan = level_.StanFile();
    const Vec3 up = { 0.0f, 1.0f, 0.0f };
    if (camera.position == cameraPos_ && camera.lookAt == cameraPos2_ && up == cameraUp_ && cameraTile_ != kNoTile) {
        return;
    }

    int tile = camera.tile;
    if (stan.WalkTiles(&tile, camera.walkStart[0], camera.walkStart[2], camera.position[0], camera.position[2])) {
        cameraTile_ = tile;
    } else if (cameraTile_ != kNoTile) {
        int previous = cameraTile_;
        if (stan.WalkTiles(&previous, cameraPos_[0], cameraPos_[2], camera.position[0], camera.position[2])) {
            cameraTile_ = previous;
        } else {
            cameraTile_ = tile;
        }
    } else {
        cameraTile_ = tile;
    }

    cameraPos_ = camera.position;
    cameraPos2_ = camera.lookAt;
    cameraUp_ = up;
    cameraRoom_ = stan.TileRoom(cameraTile_);
    cameraFloorPos_ = { cameraPos_[0], stan.PositionY(cameraTile_, cameraPos_[0], cameraPos_[2]), cameraPos_[2] };
}

PlayerPose Stage::CameraPose() const {
    PlayerPose pose;
    if (frozenCamera_ && cameraTile_ != kNoTile) {
        pose.position = cameraPos_;
        for (size_t axis = 0; axis < 3; axis++) {
            pose.look[axis] = cameraPos2_[axis] - cameraPos_[axis];
        }
        pose.up = cameraUp_;
        pose.previousPosition = cameraFloorPos_;
        pose.room = cameraRoom_;
        pose.frozen = true;
    } else {
        pose.position = player_.Position();
        pose.previousPosition = player_.PortalPosition();
        pose.look = player_.AppliedView();
        pose.up = player_.AppliedViewUp();
        pose.room = player_.PortalRoom();
    }
    switch (cameraMode_) {
        case CameraMode::Intro:
        case CameraMode::FadeSwirl:
        case CameraMode::Swirl:
        case CameraMode::DeathCamSinglePlayer:
        case CameraMode::DeathCamMultiPlayer:
            pose.cameraMode = cameraMode_;
            break;
        default:
            break;
    }
    return pose;
}

} // namespace Frigate::Level
