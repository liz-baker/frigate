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
    fog_.LoadLevelEnvironment(currentStage_, cinema, PlayerCount(), level_.VisibilityScale());
    Cur().camera.SetZRange(fog_.ZNear(), fog_.ZFar());
}

void Stage::EnterIntro() {
    SetCameraMode(CameraMode::Intro);
}

void Stage::StartBodyFade(float duration, float targetFrac) {
    if (Cur().bondChr < 0) {
        return;
    }
    Cur().colour.StartBodyFade(duration, targetFrac, chrs_[Cur().bondChr].fadeAlpha);
}

void Stage::SetCameraMode(CameraMode mode) {
    cameraMode_ = mode;
    cameraAfterCinema_ = CameraMode::None;

    // Neither a demo being recorded nor played shows the intro camera or the
    // swirl; recording is not something this port does.
    switch (mode) {
        case CameraMode::Intro:
            if (Cur().intro.selectedCamera && !demo_) {
                cameraTransitionTimer_ = 0.0f;
                Cur().colour.SetColour(0, 0, 0, 1.0f);
                Cur().colour.SetFrac(kFadeTicks, 0.0f);
                LoadFogEnvironment(/*cinema=*/true);
                Cur().cameraTile = kNoTile;
            } else {
                SetCameraMode(CameraMode::Swirl);
            }
            break;
        case CameraMode::FadeSwirl:
            Cur().colour.SetColour(0, 0, 0, 0.0f);
            Cur().colour.SetFrac(kFadeTicks, 1.0f);
            break;
        case CameraMode::MultiPlayer:
            mpSwirlRotateSpeed_ = 0.0f;
            mpSwirlAngle_ = -90.0f;
            mpSwirlForwardSpeed_ = 0.0f;
            mpSwirlDistance_ = 80.0f;
            LoadFogEnvironment(/*cinema=*/false);
            break;
        case CameraMode::Swirl:
            cameraFadeActive_ = false;
            Cur().colour.SetColour(0, 0, 0, 1.0f);
            Cur().colour.SetFrac(kFadeTicks, 0.0f);
            LoadFogEnvironment(/*cinema=*/false);
            if (!Cur().intro.swirls.empty() && !demo_) {
                cameraTransitionTimer_ = 0.0f;
                // intro_camera_index starts at 1, the record after the first
                // control point.
                introCameraIndex_ = 1;
                StartBodyFade(0.0f, 1.0f);
                LoadBondBody();
                if (Cur().bondChr >= 0) {
                    aiActions_->StartBondIntro(chrs_[Cur().bondChr], Cur().intro.animation);
                }
                Cur().cameraTile = kNoTile;
            } else {
                SetCameraMode(CameraMode::FirstPerson);
            }
            break;
        case CameraMode::FirstPerson:
            if (currentStage_ == LevelId::Cuba) {
                Cur().colour.SetColour(0, 0, 0, 1.0f);
                Cur().colour.SetFrac(0.0f, 1.0f);
            } else if (cameraFadeActive_) {
                Cur().colour.SetColour(0, 0, 0, 1.0f);
                Cur().colour.SetFrac(kFadeTicks, 0.0f);
            }
            if (Cur().weapons.Ready() && Cur().watch.State() == WatchState::Closed) {
                Cur().weapons.EquipWeapon(HandLeft, Cur().intro.startingWeaponLeft);
                Cur().weapons.EquipWeapon(HandRight, Cur().intro.startingWeaponRight);
            }
            stopTime_ = 0;
            break;
        case CameraMode::PosEnd:
            LoadBondBody();
            Cur().cameraTile = kNoTile;
            break;
        case CameraMode::FirstPersonNoInput:
            // Bond is back in the first person view, without the first person
            // mode's own set-up.
            RemoveBondBody();
            cameraMode_ = CameraMode::FirstPerson;
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
            Cur().hud.ResetIntroCameraMessages();
            SetCameraMode(CameraMode::Swirl);
            break;
        case CameraMode::Swirl:
            RemoveBondBody();
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
    const uint16_t newButtons = input.buttons & static_cast<uint16_t>(~Cur().previousButtons);

    // A pad the game reads the tile of unchecked; one that found none takes the
    // player's tile.
    const auto padTile = [&](const Pad& pad) { return pad.stanTile != kNoTile ? pad.stanTile : Cur().move.Tile(); };

    if (DeathCameraShowing()) {
        return DeathCameraTick(newButtons);
    }

    if (cameraMode_ == CameraMode::Intro || cameraMode_ == CameraMode::FadeSwirl) {
        const IntroCamera& shot = Cur().intro.cameras[*Cur().intro.selectedCamera];
        if (cameraMode_ == CameraMode::Intro) {
            const float time = cameraTransitionTimer_;
            if (time < kFirstCaptionTime && time + delta >= kFirstCaptionTime) {
                Cur().hud.SetMessageFont(HudFont::Zurich);
                Cur().hud.BottomShow(lang_.Get(shot.caption[1]));
            }
            if (shot.secondCaption != 0) {
                if (time < kSecondCaptionTime && time + delta >= kSecondCaptionTime) {
                    Cur().hud.BottomShow(lang_.Get(static_cast<uint16_t>(shot.secondCaption)));
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
        } else if (Cur().colour.IsComplete()) {
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

    if (cameraMode_ == CameraMode::PosEnd) {
        constexpr float kTau = 6.2831855f;
        if (cutscenePad_ != nullptr) {
            camera.position = cutscenePad_->position;
            camera.lookAt = Cur().move.SmoothedPosition();
            camera.tile = cutscenePad_->stanTile;
            camera.walkStart = cutscenePad_->position;
            return camera;
        }

        if (cutscene_ != nullptr) {
            const Pad* pad = setup_->FindPad(cutscene_->cameraPad);
            camera.position = cutscene_->cameraPosition;
            if (pad != nullptr) {
                camera.tile = pad->stanTile;
                camera.walkStart = pad->position;
            }
            if (cutsceneLooksAtBond_) {
                camera.lookAt = Cur().move.SmoothedPosition();
            } else {
                const float yaw = cutscene_->cameraYaw;
                const float pitch = cutscene_->cameraPitch;
                camera.lookAt = { camera.position[0] + (GameCosf(pitch) * GameSinf(yaw)),
                                  camera.position[1] + GameSinf(pitch),
                                  camera.position[2] - (GameCosf(pitch) * GameCosf(yaw)) };
            }
            return camera;
        }

        const Pad* pad = setup_->FindPad(orbit_.pad);
        if (pad == nullptr) {
            return camera;
        }
        camera.tile = pad->stanTile;
        camera.walkStart = pad->position;
        const float angle = orbit_.angle;
        camera.lookAt = { pad->position[0] + (GameCosf(angle) * 0.0f), pad->position[1] + orbit_.targetHeight,
                          pad->position[2] + (GameSinf(angle) * 0.0f) };
        camera.position = { pad->position[0] + (GameSinf(angle) * orbit_.distance) + (GameCosf(angle) * 0.0f),
                            pad->position[1] + orbit_.targetHeight + orbit_.height,
                            pad->position[2] + (GameCosf(angle) * orbit_.distance) + (GameSinf(angle) * 0.0f) };

        orbit_.angle += orbit_.speed * delta;
        while (orbit_.angle >= kTau) {
            orbit_.angle -= kTau;
        }
        while (orbit_.angle < 0.0f) {
            orbit_.angle += kTau;
        }
        return camera;
    }

    if (cameraMode_ == CameraMode::MultiPlayer) {
        // playerTickMpSwirl: the first player to move each tick steps the camera
        // shared by all of them.
        if (PositionInShuffled(current_) == 0) {
            for (int tick = 0; tick < clock_.clockTimer; tick++) {
                if (mpSwirlAngle_ < 179.5f) {
                    if (mpSwirlAngle_ < -20.0f) {
                        mpSwirlRotateSpeed_ += 0.1f;
                    }
                    if (mpSwirlAngle_ > 110.0f) {
                        mpSwirlRotateSpeed_ -= 0.1f;
                    }
                    mpSwirlAngle_ += mpSwirlRotateSpeed_;
                } else {
                    mpSwirlAngle_ = 180.0f;
                }

                if (mpSwirlAngle_ > 80.0f) {
                    if (mpSwirlDistance_ > 60.0f) {
                        mpSwirlForwardSpeed_ -= 0.1f;
                    } else {
                        mpSwirlForwardSpeed_ += 0.015f;
                    }
                    mpSwirlDistance_ += mpSwirlForwardSpeed_;
                    if (mpSwirlDistance_ < 1.0f) {
                        mpSwirlDistance_ = 1.0f;
                    }
                }
            }
        }

        const float angle = ((mpSwirlAngle_ - Cur().move.Theta()) * 3.1415927f) / 180.0f;
        const Vec3& eye = Cur().move.Position();
        camera.position = { eye[0] + (GameSinf(angle) * mpSwirlDistance_), eye[1] + (mpSwirlDistance_ * 0.08f),
                            eye[2] + (GameCosf(angle) * mpSwirlDistance_) };
        camera.lookAt = eye;
        camera.tile = objects_.Props()[Cur().move.PropId()].stan;
        camera.walkStart = { eye[0], eye[1] + (mpSwirlDistance_ * 0.08f), eye[2] };
        if (mpSwirlDistance_ < 5.0f) {
            cameraAfterCinema_ = CameraMode::Intro;
        }
        return camera;
    }

    // CAMERAMODE_SWIRL.
    const std::vector<IntroSwirl>& swirls = Cur().intro.swirls;
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
        if (Cur().colour.IsComplete()) {
            cameraAfterCinema_ = CameraMode::Intro;
        }
    }

    if (remaining > kSwirlSkipTime && !cameraFadeActive_) {
        if (!controlsLocked_ && (newButtons & kSkipButtons) != 0) {
            cameraFadeActive_ = true;
            Cur().colour.SetColour(0, 0, 0, Cur().colour.Frac());
            if (Cur().colour.IsComplete()) {
                Cur().colour.SetFrac(kFadeTicks, 1.0f);
            } else {
                Cur().colour.SetFrac(Cur().colour.FadeTime(), 1.0f);
            }
        }
    }

    const SwirlCamera swirl =
        CalcIntroSwirlCamera(swirls, introCameraIndex_, cameraTransitionTimer_, Cur().move.SmoothedPosition(),
                             Cur().move.LookDirection(), Cur().move.AppliedView());
    camera.position = swirl.position;
    camera.lookAt = swirl.lookAt;

    const int32_t swirlPad = introCameraIndex_ < swirls.size() ? swirls[introCameraIndex_].pad : -1;
    if (swirlPad >= 0) {
        const Pad& pad = setup_->Pads()[static_cast<size_t>(swirlPad)];
        camera.tile = padTile(pad);
        camera.walkStart = pad.position;
    } else {
        camera.tile = Cur().move.Tile();
        camera.walkStart = Cur().move.CollisionPosition();
    }
    return camera;
}

void Stage::SetFrozenCamera(const FrozenCamera& camera) {
    Stan& stan = level_.StanFile();
    const Vec3 up = { 0.0f, 1.0f, 0.0f };
    if (camera.position == Cur().cameraPos && camera.lookAt == Cur().cameraPos2 && up == Cur().cameraUp &&
        Cur().cameraTile != kNoTile) {
        return;
    }

    int tile = camera.tile;
    if (stan.WalkTiles(&tile, camera.walkStart[0], camera.walkStart[2], camera.position[0], camera.position[2])) {
        Cur().cameraTile = tile;
    } else if (Cur().cameraTile != kNoTile) {
        int previous = Cur().cameraTile;
        if (stan.WalkTiles(&previous, Cur().cameraPos[0], Cur().cameraPos[2], camera.position[0], camera.position[2])) {
            Cur().cameraTile = previous;
        } else {
            Cur().cameraTile = tile;
        }
    } else {
        Cur().cameraTile = tile;
    }

    Cur().cameraPos = camera.position;
    Cur().cameraPos2 = camera.lookAt;
    Cur().cameraUp = up;
    Cur().cameraRoom = stan.TileRoom(Cur().cameraTile);
    Cur().cameraFloorPos = { Cur().cameraPos[0],
                             stan.PositionY(Cur().cameraTile, Cur().cameraPos[0], Cur().cameraPos[2]),
                             Cur().cameraPos[2] };
}

PlayerPose Stage::CameraPose() const {
    PlayerPose pose;
    if (Cur().frozenCamera && Cur().cameraTile != kNoTile) {
        pose.position = Cur().cameraPos;
        for (size_t axis = 0; axis < 3; axis++) {
            pose.look[axis] = Cur().cameraPos2[axis] - Cur().cameraPos[axis];
        }
        pose.up = Cur().cameraUp;
        pose.previousPosition = Cur().cameraFloorPos;
        pose.room = Cur().cameraRoom;
        pose.frozen = true;
    } else {
        pose.position = Cur().move.Position();
        pose.previousPosition = Cur().move.PortalPosition();
        pose.look = Cur().move.AppliedView();
        pose.up = Cur().move.AppliedViewUp();
        pose.room = Cur().move.PortalRoom();
    }
    switch (cameraMode_) {
        case CameraMode::Intro:
        case CameraMode::FadeSwirl:
        case CameraMode::Swirl:
        case CameraMode::PosEnd:
        case CameraMode::MultiPlayer:
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
