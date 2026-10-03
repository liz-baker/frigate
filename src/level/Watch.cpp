#include "level/Watch.h"

#include <algorithm>
#include <cmath>

#include "game/Sound.h"
#include "gfx/GbiF3dex.h"
#include "level/GunDraw.h"
#include "level/ModelHandle.h"
#include "level/Matrix.h"
#include "model/Quaternion.h"
#include "model/Random.h"
#include "model/RenderState.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kDegToRad = 0.017453292f;
// FLT_EPSILON * 16.
constexpr float kEpsilon = 1.1920929e-07f * 16.0f;

// The watch's field of view when it is open, and when it is closed.
constexpr float kWatchFovY = 5.9f;
constexpr float kClosedFovY = 60.0f;

// The hand model that shows the watch, its slot in the body table and the
// switch nodes it reads by number.
constexpr int32_t kSuitLfHand = 84;
constexpr size_t kSlotHourHand = 0;
constexpr size_t kSlotMinuteHand = 1;
constexpr size_t kSlotSecondHand = 2;
constexpr size_t kSlotFace = 3;
constexpr size_t kSlotCuffs = 4;
// The body table entry of that model (BODY_Left_Suit_Hand_Floating_Arm), whose
// scale sizes the watch.
constexpr float kBodyScale = 0.1f;
constexpr float kModelScale = 0.10000001f;

// The view's aspect (VIEWPORT_WIDTH / VIEWPORT_HEIGHT_DEFAULT of the US game)
// and depth range the watch is drawn over.
constexpr float kWatchAspect = 1.4545455f;
constexpr float kWatchNear = 10.0f;
constexpr float kWatchFar = 300.0f;

// The gun code's reasons, which the watch passes the wrong way round: it
// hides the sight with the ammo counter's damage reason and the ammo counter
// with the sight's no-control reason.
constexpr int32_t kSightHiddenBit = AmmoReason::Damage;
constexpr int32_t kAmmoHiddenBit = SightReason::NoControl;

Mtxf ToMtxf(const Mat4& matrix) {
    Mtxf out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

Mtxf ToMtxf(const Gu::MatrixF& matrix) {
    Mtxf out{};
    std::copy(&matrix[0][0], &matrix[0][0] + 16, &out.m[0][0]);
    return out;
}

// matrix_4x4_set_position_and_rotation_around_y.
Mtxf PositionAndRotationY(const Vec3& position, float angle) {
    Mtxf out = RotationY(angle);
    out.m[3][0] = position[0];
    out.m[3][1] = position[1];
    out.m[3][2] = position[2];
    return out;
}

// matrix_4x4_get_rotation_around_xyz: the angles, in radians, that
// RotationXYZ builds the matrix from.
Vec3 RotationAnglesOf(const Mtxf& matrix) {
    const float sinXCosY = matrix.m[1][2];
    const float cosXCosY = matrix.m[2][2];
    const float norm = std::sqrt((sinXCosY * sinXCosY) + (cosXCosY * cosXCosY));
    if (kEpsilon < norm) {
        return { GameAtan2f(matrix.m[1][2], matrix.m[2][2]), GameAtan2f(-matrix.m[0][2], norm),
                 GameAtan2f(matrix.m[0][1], matrix.m[0][0]) };
    }
    return { 0.0f, GameAtan2f(-matrix.m[0][2], norm), GameAtan2f(-matrix.m[1][0], matrix.m[1][1]) };
}

// quaternion_to_matrix.
Mtxf QuaternionToMatrix(const Quaternion& q) {
    const float scale = 2.0f / ((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    const float xs = q.x * scale;
    const float ys = q.y * scale;
    const float zs = q.z * scale;
    const float wx = q.w * xs;
    const float wy = q.w * ys;
    const float wz = q.w * zs;
    const float xx = q.x * xs;
    const float xy = q.x * ys;
    const float xz = q.x * zs;
    const float yy = q.y * ys;
    const float yz = q.y * zs;
    const float zz = q.z * zs;
    Mtxf out = IdentityMtxf();
    out.m[0][0] = 1.0f - (yy + zz);
    out.m[0][1] = xy + wz;
    out.m[0][2] = xz - wy;
    out.m[1][0] = xy - wz;
    out.m[1][1] = 1.0f - (xx + zz);
    out.m[1][2] = yz + wx;
    out.m[2][0] = xz + wy;
    out.m[2][1] = yz - wx;
    out.m[2][2] = 1.0f - (xx + yy);
    return out;
}

Vec3 NodeOrigin(const ModelNode& node) {
    return { node.origin[0], node.origin[1], node.origin[2] };
}

} // namespace

void Watch::Attach(PlayerMove* player, PlayerWeapons* weapons, Hud* hud, ModelFactory* models) {
    player_ = player;
    weapons_ = weapons;
    hud_ = hud;
    models_ = models;
}

void Watch::Reset(int32_t watchTime) {
    state_ = WatchState::Closed;
    pauseTime_ = 0;
    timer_ = 0;
    pausingFlag_ = false;
    controlsLocked_ = false;
    soundPending_ = true;
    pauseStartingAngle_ = 0.0f;
    pauseSavedVerta_ = 0.0f;
    pauseTargetVerta_ = 0.0f;
    pauseTransitionTime_ = 0.0f;
    pauseTransitionDuration_ = 0.0f;
    pauseState_ = 0;
    stepInView_ = 0;
    pauseAnimationCounter_ = 0.0f;
    pauseWatchRelated_ = 0.0f;
    pauseWatchRelatedScaled_ = 0.0f;
    zoomFovY_ = kClosedFovY;
    zoomTime_ = 0.0f;
    zoomTimeMax_ = 0.0f;
    zoomFovOld_ = kClosedFovY;
    zoomFovNew_ = kClosedFovY;
    transitionTime_ = 0.9090909f;
    watchTime_ = watchTime;
    targetPosition_ = { 0.0f, 0.0f, -25.0f };
    targetLook_ = { 0.0f, 1.0f, 0.0f };
    targetUp_ = { 0.0f, 0.0f, 1.0f };
    model_.reset();
    menu_.Reset();
}

void Watch::AttachMenu(WatchMenuHost* host, PlayerOptions* options, const Inventory* inventory) {
    WatchMenu::Links links;
    host_ = host;
    links.host = host;
    links.owner = this;
    links.hud = hud_;
    links.weapons = weapons_;
    links.options = options;
    links.inventory = inventory;
    links.models = models_;
    links.random = Frigate::RandomGetNext;
    menu_.Attach(links);
}

void Watch::BeginFrame() {
    generation_ ^= 1;
    draws_[generation_].clear();
    menu_.BeginFrame();
}

float Watch::PauseAnimationPercent() const {
    if (stepInView_ == 1 || stepInView_ == 2) {
        return pauseAnimationCounter_ / 20.0f;
    }
    if (stepInView_ == 3) {
        return 1.0f;
    }
    return 0.0f;
}

// bondviewInitPauseTransition.
void Watch::InitPauseTransition() {
    pauseStartingAngle_ = player_->Verta();
    pauseTransitionTime_ = 0.0f;
    pauseState_ = 0;
}

// bondviewSetupPauseTransition: sets the tilt's start and end pitch, and works
// out how long the tilt takes: the further the view has to turn to the pause
// pitch of -40 degrees, the longer.
float Watch::SetupPauseTransition(bool toPause) {
    if (toPause) {
        pauseSavedVerta_ = player_->Verta();
        pauseTargetVerta_ = -40.0f;
    } else {
        pauseSavedVerta_ = pauseStartingAngle_;
        pauseTargetVerta_ = player_->Verta();
    }

    float difference = pauseSavedVerta_ - pauseTargetVerta_;
    if (difference < 0.0f) {
        difference = -difference;
    }

    float duration;
    if (difference >= 60.0f) {
        duration = (((difference - 60.0f) * 60.0f * 0.5f) / 60.0f) + 60.0f;
    } else if (difference <= 0.0f) {
        duration = 0.0f;
    } else {
        duration = (difference * 60.0f) / 60.0f;
    }
    return duration;
}

void Watch::StartPauseTransition(float duration) {
    pauseTransitionTime_ = 0.0f;
    pauseTransitionDuration_ = duration;
    pauseState_ = 1;
}

void Watch::StartUnpauseTransition(float duration) {
    pauseTransitionTime_ = 0.0f;
    pauseTransitionDuration_ = duration;
    pauseState_ = 2;
}

bool Watch::IsPauseTransitioning() const {
    return pauseState_ != 0 && pauseState_ != 3;
}

// bondviewUpdatePauseTransition: tilts the view to the watch's pitch, and back
// once the game is unpaused.
void Watch::UpdatePauseTransition() {
    const float previous = player_->Verta();
    float verta = previous;

    if (pauseState_ == 1) {
        pauseTransitionTime_ += timing_.globalTimerDelta * transitionTime_;
        if (pauseTransitionTime_ < pauseTransitionDuration_) {
            const float fraction = pauseTransitionTime_ / pauseTransitionDuration_;
            const float weight = (1.0f - GameCosf((fraction * kTau) * 0.5f)) * 0.5f;
            verta = pauseSavedVerta_ + ((pauseTargetVerta_ - pauseSavedVerta_) * weight);
        } else {
            verta = pauseTargetVerta_;
            pauseState_ = 3;
        }
    } else if (pauseState_ == 2) {
        pauseTransitionTime_ += timing_.globalTimerDelta * transitionTime_;
        if (pauseTransitionTime_ < pauseTransitionDuration_) {
            const float fraction = pauseTransitionTime_ / pauseTransitionDuration_;
            const float weight = (1.0f - GameCosf((fraction * kTau) * 0.5f)) * 0.5f;
            verta = pauseTargetVerta_ + ((pauseSavedVerta_ - pauseTargetVerta_) * weight);
        } else {
            verta = pauseSavedVerta_;
            pauseState_ = 0;
        }
    }

    if (verta < -180.0f) {
        verta += 360.0f;
    } else if (verta >= 180.0f) {
        verta -= 360.0f;
    }
    player_->SetVerta(verta);

    // The tilt's speed from the last tick's pitch, kept within +-0.7 so the
    // gun's sway never sees it move too fast.
    if (timing_.clockTimer > 0) {
        float speed = verta - previous;
        if (speed < 0.0f) {
            speed += 360.0f;
        }
        if (speed > 180.0f) {
            speed -= 360.0f;
        }
        speed /= timing_.globalTimerDelta + timing_.globalTimerDelta;
        if (speed < -0.7f) {
            speed = -0.7f;
        } else if (speed > 0.7f) {
            speed = 0.7f;
        }
        player_->SetSpeedVerta(speed);
    }
}

// trigger_watch_zoom.
void Watch::TriggerZoom(float target, float time) {
    zoomTime_ = 0.0f;
    zoomTimeMax_ = time;
    zoomFovOld_ = zoomFovY_;
    zoomFovNew_ = target;
}

// bondviewGetWatchZoomFovy.
float Watch::ZoomTarget() const {
    if (zoomTime_ < zoomTimeMax_) {
        return zoomFovNew_;
    }
    return zoomFovY_;
}

// bondviewTriggerWatchZoom.
void Watch::TriggerZoomTo(float fovY) {
    if (ZoomTarget() != fovY) {
        if (fovY < zoomFovY_) {
            TriggerZoom(fovY, ((zoomFovY_ - fovY) * 15.0f) / 30.0f);
            return;
        }
        TriggerZoom(fovY, ((fovY - zoomFovY_) * 15.0f) / 30.0f);
    }
}

// bondviewZoomToWatchOnOpen.
void Watch::ZoomToWatchOnOpen() {
    float time = ((kWatchFovY - zoomFovY_) * 45.0f) / -54.1f;
    if (time < 0.0f) {
        time = -time;
    }
    TriggerZoom(kWatchFovY, time);
}

// bondviewZoomFromWatchOnExit.
void Watch::ZoomFromWatchOnExit() {
    float time = ((kClosedFovY - zoomFovY_) * 45.0f) / -54.1f;
    if (time < 0.0f) {
        time = -time;
    }
    TriggerZoom(kClosedFovY, time);
}

// check_watch_page_transistion_running.
bool Watch::ZoomRunning() const {
    return zoomTime_ < zoomTimeMax_;
}

// The scale draw_background_health_and_armor gives the face while a page
// change zooms: how far the zoom has gone, squared and kept to 1.
float Watch::PageZoomScale() const {
    float scale = (zoomTime_ * (zoomFovNew_ - zoomFovOld_)) / zoomTimeMax_;
    if (scale < 0.0f) {
        scale = -scale;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    return scale * scale;
}

// bondviewWatchAnimationRelated.
float Watch::UnfoldScale() const {
    if (state_ == WatchState::ZoomIn) {
        return ((45.0f - zoomTimeMax_) + zoomTime_) / 45.0f;
    }
    if (state_ == WatchState::ZoomOut) {
        return (zoomTimeMax_ - zoomTime_) / 45.0f;
    }
    if (state_ == WatchState::Open || state_ == WatchState::Closing) {
        return 1.0f;
    }
    return 0.0f;
}

// bondviewUpdateWatchZoomIn.
void Watch::UpdateZoomIn() {
    if (zoomTime_ < zoomTimeMax_) {
        if (state_ == WatchState::Open || state_ == WatchState::Closing) {
            zoomTime_ += static_cast<float>(timing_.speedFrames);
        } else {
            zoomTime_ += static_cast<float>(timing_.speedFrames) * transitionTime_;
        }
        if (zoomTimeMax_ < zoomTime_) {
            zoomTime_ = zoomTimeMax_;
        }
        zoomFovY_ = zoomFovOld_ + ((zoomTime_ * (zoomFovNew_ - zoomFovOld_)) / zoomTimeMax_);
    } else {
        zoomTime_ = zoomTimeMax_;
        zoomFovY_ = zoomFovNew_;
    }
}

void Watch::UpdateZoom(float target) {
    TriggerZoomTo(target);
    UpdateZoomIn();
}

// bondviewSetPauseWatchRelated and bondviewSetPauseWatchRelatedAlt: set how
// fast the arm is raised or lowered.
void Watch::SetPauseWatchRelated(float value) {
    if (stepInView_ == 0) {
        pauseWatchRelatedScaled_ = 20.0f / value;
    } else {
        pauseWatchRelatedScaled_ = (20.0f - CurrentFrame()) / value;
    }
    stepInView_ = 1;
    pauseWatchRelated_ = value;
}

void Watch::SetPauseWatchRelatedAlt(float value) {
    if (stepInView_ == 3) {
        pauseWatchRelatedScaled_ = 20.0f / value;
    } else {
        pauseWatchRelatedScaled_ = CurrentFrame() / value;
    }
    stepInView_ = 2;
    pauseWatchRelated_ = value;
}

float Watch::CurrentFrame() const {
    return model_ != nullptr ? model_->playback.frame : 0.0f;
}

// bondviewStepWatchAnimation.
void Watch::StepWatchAnimation() {
    if (stepInView_ != 0 && stepInView_ != 3) {
        if (stepInView_ == 1) {
            pauseAnimationCounter_ += timing_.globalTimerDelta * transitionTime_ * pauseWatchRelatedScaled_;
            if (pauseAnimationCounter_ > 20.0f) {
                pauseAnimationCounter_ = 20.0f;
                stepInView_ = 3;
            }
        } else if (stepInView_ == 2) {
            pauseAnimationCounter_ -= timing_.globalTimerDelta * transitionTime_ * pauseWatchRelatedScaled_;
            if (pauseAnimationCounter_ < 0.0f) {
                pauseAnimationCounter_ = 0.0f;
                stepInView_ = 0;
            }
        }
        if (model_ != nullptr && model_->playback.anim != nullptr) {
            model_->playback.SetAnimFrame2(pauseAnimationCounter_, 0.0f);
        }
    }
}

// sub_GAME_7F07E7CC: the watch model, its scale and its animation.
void Watch::InitModel() {
    if (models_ == nullptr || weapons_ == nullptr) {
        return;
    }
    const ItemStats& item = weapons_->Table().Item(kSuitLfHand);
    if (item.model.empty()) {
        return;
    }
    model_ = models_->CreateGun(item.model);
    if (model_ == nullptr) {
        return;
    }
    model_->playback.scale = kBodyScale * kModelScale;
    if (const AnimationClip* clip = models_->LoadAnimation(*model_, "bond_watch")) {
        model_->playback.SetAnimation(clip, false, 0.0f, 0.5f * transitionTime_, 0.0f);
    }
    stepInView_ = 0;
}

void Watch::OnInput(bool startPressed, bool blocked) {
    if (!blocked &&
        ((state_ != WatchState::Open && startPressed) || (state_ == WatchState::Open && menu_.CloseRequested()))) {
        Trigger(false);
    }
}

void Watch::EnterState(int32_t state) {
    state_ = state;
    pauseTime_ = 0;
    timer_ = 0;
}

void Watch::Trigger(bool interrupt) {
    if (state_ == WatchState::Closed) {
        if (interrupt) {
            return;
        }
        transitionTime_ *= 1.1f;
        if (transitionTime_ > 1.7f) {
            transitionTime_ = 1.7f;
        }

        // Both hands are lowered before the arm comes up, or waited for if
        // one is still changing weapon.
        const int32_t left = weapons_->Hands(HandLeft).actionState;
        const bool leftSettled = left != GunState::SwitchLower && left != GunState::SwitchSwap &&
                                 left != GunState::SwitchHold && left != GunState::SwitchRaise;
        state_ = weapons_->HandWithoutItem(HandLeft) && weapons_->HandWithoutItem(HandRight) && leftSettled
                     ? WatchState::LowerWeapon
                     : WatchState::WaitForHands;
        pauseTime_ = 0;
        timer_ = 0;

        InitPauseTransition();
        TriggerZoomTo(kClosedFovY);
        menu_.Begin();
        return;
    }

    // The watch is already coming up or going away: Start turns it round, and
    // the player dying (`interrupt`) winds it down.
    int32_t next = state_;
    switch (state_) {
        case WatchState::LowerWeapon:
            next = WatchState::Interrupted1;
            break;
        case WatchState::TiltView:
            next = WatchState::Interrupted2;
            break;
        case WatchState::RaiseArm:
            next = WatchState::LowerArm;
            break;
        case WatchState::ZoomIn:
            next = WatchState::ZoomOut;
            break;
        case WatchState::Open:
            menu_.Exit();
            next = WatchState::Closing;
            menu_.ClearCloseRequest();
            break;
        case WatchState::ZoomOut:
            next = interrupt ? state_ : WatchState::ZoomIn;
            break;
        case WatchState::LowerArm:
            next = interrupt ? state_ : WatchState::RaiseArm;
            break;
        case WatchState::TiltBack:
            next = interrupt ? state_ : WatchState::Interrupted3;
            break;
        case WatchState::Interrupted1:
        case WatchState::Interrupted2:
            next = interrupt ? state_ : WatchState::LowerWeapon;
            break;
        case WatchState::Interrupted3:
            next = WatchState::TiltBack;
            break;
        case WatchState::WaitForHands:
            state_ = WatchState::Closed;
            return;
        default:
            break;
    }
    if (next == state_) {
        return;
    }
    // The watch turned back towards the menu from zooming out, lowering the
    // arm or tilting back starts the menu's pages over.
    const bool restartsMenu = !interrupt && (state_ == WatchState::ZoomOut || state_ == WatchState::LowerArm ||
                                             state_ == WatchState::TiltBack);
    EnterState(next);
    if (restartsMenu) {
        menu_.ResetPages();
    }
}

void Watch::Tick() {
    if (state_ != WatchState::Closed) {
        const bool bothHandsHold =
            weapons_->WeaponId(HandRight) == kItemTrigger || weapons_->WeaponId(HandRight) == kItemWatchLaser;
        // How long the hands take to lower into the watch's position.
        const int32_t handWait = bothHandsHold ? 35 : 17;

        timer_ += timing_.clockTimer;
        pauseTime_ += 1;
        pausingFlag_ = false;

        if (state_ == WatchState::LowerWeapon) {
            pausingFlag_ = false;

            if (pauseTime_ == 1) {
                weapons_->DrawItemInHand(HandLeft, kSuitLfHand);
            } else if (pauseTime_ == 2) {
                if (bothHandsHold) {
                    weapons_->DrawItemInHand(HandRight, kItemUnarmed);
                }
            } else {
                if (weapons_->ItemInHandOrWatchMenu(HandLeft) == kSuitLfHand && weapons_->HandWithoutItem(HandLeft) &&
                    (!bothHandsHold || (weapons_->ItemInHandOrWatchMenu(HandRight) == kItemUnarmed &&
                                        weapons_->HandWithoutItem(HandRight))) &&
                    timer_ >= handWait) {
                    EnterState(WatchState::TiltView);
                    pauseTime_ = 1;
                }
            }
        }

        if (state_ == WatchState::TiltView) {
            pausingFlag_ = false;

            if (pauseTime_ == 1) {
                hud_->SetSightVisible(kSightHiddenBit, false);
                hud_->SetAmmoVisible(kAmmoHiddenBit, false);
                hud_->MessagesOff(HudFlag::LockControls);
                hud_->SetUpperTextFlag(HudFlag::LockControls);
                // TODO(https://github.com/liz-baker/frigate/issues/225):
                // countdownTimerSetVisible(4, 0), hiding the mission clock.

                if (pauseState_ == 0 || pauseState_ == 2 || pauseState_ == 3) {
                    float duration = SetupPauseTransition(true);
                    if (duration < 30.0f) {
                        duration = 30.0f;
                    }
                    StartPauseTransition(duration);
                }
            }

            if ((pauseTransitionDuration_ - pauseTransitionTime_) < 30.0f) {
                EnterState(WatchState::RaiseArm);
                pauseTime_ = 1;
                InitModel();
            }
        }

        if (state_ == WatchState::RaiseArm) {
            if (pauseTime_ == 1) {
                float span = 40.0f;
                if (stepInView_ != 0 && stepInView_ != 3) {
                    span = ((20.0f - CurrentFrame()) * 40.0f) / 20.0f;
                }

                if (pauseState_ == 0 || pauseState_ == 2 || pauseState_ == 3) {
                    float duration = SetupPauseTransition(true);
                    float shortest = span - 10.0f;
                    if (duration < shortest) {
                        duration = shortest;
                    }
                    StartPauseTransition(duration);

                    shortest = duration + 10.0f;
                    if (span < shortest) {
                        span = shortest;
                    }
                }

                SetPauseWatchRelated(span);
            }

            if ((stepInView_ != 0 && stepInView_ != 3) || IsPauseTransitioning()) {
                pausingFlag_ = true;
            } else {
                EnterState(WatchState::ZoomIn);
                pauseTime_ = 1;
            }
        }

        if (state_ == WatchState::ZoomIn) {
            if (pauseTime_ == 1 && soundPending_) {
                Game::Sound::PlaySfx(AudioDriver::kSfxWatchOn);
                soundPending_ = false;
            }

            ZoomToWatchOnOpen();

            if (ZoomRunning()) {
                pausingFlag_ = true;
            } else {
                EnterState(WatchState::Open);
                pauseTime_ = 1;
                soundPending_ = true;
            }
        }

        if (state_ == WatchState::Open) {
            if (pauseTime_ == 1 && host_ != nullptr) {
                host_->EnterWatchMusic();
            }
            pausingFlag_ = true;
        }

        if (state_ == WatchState::Closing) {
            pausingFlag_ = true;

            if (pauseTime_ >= 3) {
                EnterState(WatchState::ZoomOut);
                pauseTime_ = 1;
                Game::Sound::PlaySfx(AudioDriver::kSfxWatchOff);
            }
        }

        if (state_ == WatchState::ZoomOut) {
            if (pauseTime_ == 1) {
                ZoomFromWatchOnExit();
                if (bothHandsHold) {
                    weapons_->DrawItemInHand(HandRight, kItemUnarmed);
                } else {
                    weapons_->RestoreFromWatchMenu(HandRight);
                }
            }
            if (ZoomRunning()) {
                pausingFlag_ = true;
            } else {
                EnterState(WatchState::LowerArm);
                pauseTime_ = 1;
                soundPending_ = true;
            }
        }

        if (state_ == WatchState::LowerArm) {
            if (pauseTime_ == 1) {
                float span = 40.0f;
                float duration = SetupPauseTransition(false);

                if (stepInView_ != 0 && stepInView_ != 3) {
                    span = (CurrentFrame() * 40.0f) / 20.0f;
                }

                const float shortest = span + 20.0f;
                if (duration < shortest) {
                    duration = shortest;
                }

                StartUnpauseTransition(duration);
                SetPauseWatchRelatedAlt(span);
            }

            if (stepInView_ != 0 && stepInView_ != 3) {
                pausingFlag_ = true;
            } else {
                EnterState(WatchState::TiltBack);
                pauseTime_ = 1;
            }
        }

        if (state_ == WatchState::TiltBack) {
            pausingFlag_ = false;

            if (pauseTime_ == 1) {
                if (weapons_->ItemInHandOrWatchMenu(HandLeft) != weapons_->WeaponId(HandLeft) &&
                    weapons_->HandWithoutItem(HandLeft)) {
                    weapons_->DrawItemInHand(HandLeft, weapons_->WeaponId(HandLeft));
                }

                if (bothHandsHold) {
                    if (weapons_->ItemInHandOrWatchMenu(HandRight) != weapons_->WeaponId(HandRight) &&
                        weapons_->HandWithoutItem(HandRight)) {
                        weapons_->DrawItemInHand(HandRight, weapons_->WeaponId(HandRight));
                    }
                }
            } else if (!IsPauseTransitioning()) {
                if (weapons_->ItemInHandOrWatchMenu(HandLeft) == weapons_->WeaponId(HandLeft) &&
                    weapons_->HandWithoutItem(HandLeft) &&
                    (!bothHandsHold || (weapons_->ItemInHandOrWatchMenu(HandRight) == weapons_->WeaponId(HandRight) &&
                                        weapons_->HandWithoutItem(HandRight)))) {
                    weapons_->RestoreFromWatchMenu(HandLeft);
                    if (bothHandsHold) {
                        weapons_->RestoreFromWatchMenu(HandRight);
                    }

                    hud_->SetSightVisible(kSightHiddenBit, true);
                    hud_->SetAmmoVisible(kAmmoHiddenBit, true);
                    hud_->MessagesOn(1);
                    hud_->ClearUpperTextFlag(1);
                    // TODO(https://github.com/liz-baker/frigate/issues/225):
                    // countdownTimerSetVisible(4, 1).

                    state_ = WatchState::Closed;
                    pauseTime_ = 0;
                    timer_ = 0;
                }
            }
        }

        if (state_ == WatchState::Interrupted1) {
            pausingFlag_ = false;

            if (weapons_->WeaponId(HandLeft) != weapons_->ItemInHandOrWatchMenu(HandLeft) &&
                weapons_->HandWithoutItem(HandLeft) &&
                (!bothHandsHold || (weapons_->WeaponId(HandRight) != weapons_->ItemInHandOrWatchMenu(HandRight) &&
                                    weapons_->HandWithoutItem(HandRight)))) {
                EnterState(WatchState::TiltBack);
                pauseTime_ = 0;
            }
        }

        if (state_ == WatchState::Interrupted2) {
            pausingFlag_ = false;

            if (pauseTime_ == 1) {
                StartUnpauseTransition(SetupPauseTransition(false));
            }

            if (weapons_->WeaponId(HandLeft) != weapons_->ItemInHandOrWatchMenu(HandLeft) &&
                weapons_->HandWithoutItem(HandLeft) &&
                (!bothHandsHold || (weapons_->WeaponId(HandRight) != weapons_->ItemInHandOrWatchMenu(HandRight) &&
                                    weapons_->HandWithoutItem(HandRight)))) {
                EnterState(WatchState::TiltBack);
                pauseTime_ = 0;
            }
        }

        if (state_ == WatchState::Interrupted3) {
            pausingFlag_ = false;

            if (weapons_->WeaponId(HandLeft) == weapons_->ItemInHandOrWatchMenu(HandLeft) &&
                weapons_->HandWithoutItem(HandLeft) &&
                (!bothHandsHold || (weapons_->WeaponId(HandRight) == weapons_->ItemInHandOrWatchMenu(HandRight) &&
                                    weapons_->HandWithoutItem(HandRight)))) {
                EnterState(WatchState::LowerWeapon);
                pauseTime_ = 0;
            }
        }

        if (state_ == WatchState::WaitForHands) {
            pausingFlag_ = false;

            if (weapons_->HandWithoutItem(HandLeft) && weapons_->HandWithoutItem(HandRight)) {
                const int32_t left = weapons_->Hands(HandLeft).actionState;
                if (left != GunState::SwitchLower && left != GunState::SwitchSwap && left != GunState::SwitchHold &&
                    left != GunState::SwitchRaise) {
                    EnterState(WatchState::LowerWeapon);
                    pauseTime_ = 0;
                }
            }
        }

        UpdatePauseTransition();
        StepWatchAnimation();
        UpdateZoomIn();
    }

    if (state_ == WatchState::Open) {
        controlsLocked_ = true;
        menu_.Tick(timing_.speedFrames);
    } else if (state_ == WatchState::Closing) {
        controlsLocked_ = true;
    } else {
        controlsLocked_ = false;
    }
}

Gfx* Watch::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const WatchView& view) {
    using namespace Frigate::F3dex;
    if (state_ == WatchState::Closed || !pausingFlag_ || model_ == nullptr || view.worldToScreen == nullptr) {
        return dl;
    }
    Model::ModelInstance& model = *model_;

    Gu::MatrixF perspective;
    Gu::PerspectiveF(perspective, zoomFovY_, kWatchAspect, kWatchNear, kWatchFar, 1.0f);
    projection_ = Mtx::ToFixedMtx(perspective);
    *dl++ = Matrix(&projection_, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    model.SetSwitchVisible(kSlotFace, menu_.OutsideMenu());

    // The watch is placed at the player's head, a step in front of it.
    const Vec3 body = player_->HeadBodyOffset();
    const Vec3& look = player_->LookDirection();
    const Vec3& position = player_->CollisionPosition();
    const Vec3 watchPosition = {
        (look[0] * (body[2] + -12.0f)) + (position[0] + (body[0] * -look[2])),
        body[1] + position[1],
        (look[2] * (body[2] + -12.0f)) + (position[2] + (body[0] * look[0])),
    };
    Mtxf watchMtx = PositionAndRotationY(watchPosition, (360.0f - player_->Theta()) * kDegToRad);
    watchMtx = MultiplyHomogeneous(ToMtxf(*view.worldToScreen), watchMtx);

    SelectCuff(model, view.bondType, kSlotCuffs);
    model.Pose(ToMat4(watchMtx));

    const ModelNode* hourNode = model.SwitchNode(kSlotHourHand);
    const ModelNode* minuteNode = model.SwitchNode(kSlotMinuteHand);
    const ModelNode* secondNode = model.SwitchNode(kSlotSecondHand);
    if (hourNode == nullptr || minuteNode == nullptr || secondNode == nullptr || model.Walk().matrices.size() < 4) {
        return dl;
    }

    Mtxf base = ToMtxf(model.Walk().matrices[0]);
    const Vec3 hourOrigin = NodeOrigin(*hourNode);
    const float scale = model.playback.scale;

    // The arm swings from where its animation puts it to a fixed place in
    // front of the view as the animation plays out.
    float t = CurrentFrame() / 20.0f;
    if (t > 1.0f) {
        t = 1.0f;
    }
    const Vec3 targetPosition = {
        base.m[3][0] + (((targetPosition_[0] - (hourOrigin[0] * scale)) - base.m[3][0]) * t),
        base.m[3][1] + (((targetPosition_[1] + (hourOrigin[2] * scale)) - base.m[3][1]) * t),
        base.m[3][2] + (((targetPosition_[2] - (hourOrigin[1] * scale)) - base.m[3][2]) * t),
    };

    const Mtxf targetMtx =
        BasisToward({ targetLook_[0], targetLook_[1], targetLook_[2] }, { targetUp_[0], targetUp_[1], targetUp_[2] });
    const Vec3 currentRotation = RotationAnglesOf(base);
    const Vec3 targetRotation = RotationAnglesOf(targetMtx);
    const Quaternion from = EulerToQuaternion({ currentRotation[0], currentRotation[1], currentRotation[2] });
    Quaternion to = EulerToQuaternion({ targetRotation[0], targetRotation[1], targetRotation[2] });
    to = EnsureShortestPath(from, to);
    Mtxf placed = QuaternionToMatrix(Slerp(from, to, t));
    placed.m[3][0] = targetPosition[0];
    placed.m[3][1] = targetPosition[1];
    placed.m[3][2] = targetPosition[2];
    ScaleRows(&placed, scale);

    // The hands turn to the time the intro set, running on.
    const int32_t time = watchTime_;
    const int32_t totalSeconds = time / 60;
    const int32_t seconds = totalSeconds % 60;
    const int32_t totalMinutes = totalSeconds / 60;
    const int32_t minutes = totalMinutes % 60;
    const float framesFraction = static_cast<float>(time % 60) / 60.0f;
    float secondsAngle = ((-(static_cast<float>(seconds) + framesFraction)) * kTau) / 60.0f;
    float minutesAngle = ((((-static_cast<float>(minutes)) * kTau) / 60.0f) * 1.0f) + (secondsAngle / 60.0f);
    float hoursAngle = ((((-static_cast<float>((totalSeconds / 3600) % 12)) * kTau) / 12.0f) + (minutesAngle / 12.0f)) +
                       (secondsAngle / 720.0f);
    while (secondsAngle < 0.0f) {
        secondsAngle += kTau;
    }
    while (minutesAngle < 0.0f) {
        minutesAngle += kTau;
    }
    while (hoursAngle < 0.0f) {
        hoursAngle += kTau;
    }

    const auto handMatrix = [&placed](const ModelNode& node, float angle) {
        return Multiply(placed, PositionAndRotationY(NodeOrigin(node), angle));
    };

    std::vector<Mat4> matrices = model.Walk().matrices;
    matrices[0] = ToMat4(placed);
    matrices[1] = ToMat4(handMatrix(*hourNode, hoursAngle));
    matrices[2] = ToMat4(handMatrix(*minuteNode, minutesAngle));
    matrices[3] = ToMat4(handMatrix(*secondNode, secondsAngle));
    model.ReplaceMatrices(std::move(matrices));

    ModelRenderParams params;
    params.zbufferEnabled = false;
    params.propType = kRenderPropChr;
    params.passes = 3;
    params.sortJoints = false;
    params.envColour = (state_ == WatchState::Open || state_ == WatchState::Closing) ? 0xCD : view.tileColour;

    auto draw = Model::BuildJointDraw(resourceManager, { model_ }, params);
    if (draw) {
        Model::JointDraw& kept = draws_[generation_].emplace_back(std::move(*draw));
        for (const Gfx& command : kept.commands) {
            *dl++ = command;
        }
    }

    // draw_watch_current_page, over the face: the second hand's origin is where
    // the menu's coordinates start.
    faceMatrix_ = Mtx::ToFixedMtx(handMatrix(*secondNode, 0.0f).m);
    return menu_.Render(dl, resourceManager, &faceMatrix_, state_ == WatchState::Open || state_ == WatchState::Closing,
                        view.menu);
}

} // namespace Frigate::Level
