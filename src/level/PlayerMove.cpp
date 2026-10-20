#include "level/PlayerMove.h"

#include <cmath>

#include "game/Sound.h"
#include "level/ApplySpeed.h"
#include "level/Matrix.h"
#include "level/PropList.h"
#include "level/StanIntersection.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kDegToRad = 0.017453292f;
constexpr float kFovY = 60.0f;

// US values of the constants bondview2.c defines per region.
constexpr float kChrObjAccel = 0.5f;
constexpr float kChrObjMaxSpeed = 5.0f;
constexpr float kMaxSpeedFactor = 0.8f;
constexpr int kThreeSecondTicks = 180;
constexpr float kStanHeightScale = 0.83f;
constexpr float kClippingClockFactor = 0.8f;
constexpr float kClippingBounceFactor = 0.19999999f;
constexpr int kClippingBounceTicks = 15;
constexpr float kClippingBounceDecay = -4.5f;
// FIELD_3B8_FACTOR and the factor the smoothed position is scaled back by.
constexpr float kSmoothedPositionScale = 0.9f;
constexpr float kSmoothedPositionFactor = 0.100000024f;
constexpr float kSmoothedPositionInitial = 0.100000023842f;

constexpr float kFullCrouchOffset = -100.0f;
constexpr float kHalfCrouchOffset = -60.0f;
constexpr float kSpeedRegularMax = 1.0f;
constexpr float kSpeedRunMax = 1.25f;
constexpr float kSpeedTickAdjust = 0.01f;

// The eye is this far above the head's height.
constexpr float kEyeOffset = 7.0f;
constexpr float kMinimumEyeReach = 30.0f;

// Stick reading of a 'turn' or 'pitch': the stick's full range.
constexpr float kStickRange = 70.0f;

// bondviewProcessInput: the stick beyond this counts as pushed hard over.
constexpr int kAimStickThreshold = 60;

// The view turns and pitches by its speed times the tick time times this.
constexpr float kVertaPerSpeedTick = 3.5f;

// Degrees a tick at the decomp's full-stick turn or pitch speed moves the view.
constexpr float kFullStickDegreesPerTick = kVertaPerSpeedTick;

int SafeStick(int stick) {
    if (stick < -5) {
        return stick + 5;
    }
    if (stick > 5) {
        return stick - 5;
    }
    return 0;
}

float CapAtOne(float value) {
    return value > 1.0f ? 1.0f : value;
}

// viGetFovY() / FOV_Y_F, and sub_GAME_7F07FF74: the speed the view turns at for
// a full stick.
float TurnLimit(float fovY, float value) {
    if (value > 0) {
        return (fovY * value * -0.7f) / kFovY;
    }
    if (value < 0) {
        return (fovY * -value * 0.7f) / kFovY;
    }
    return 0;
}

} // namespace

void PlayerMove::UpdateAppliedView(const Vec3& look, const Vec3& up) {
    Mtxf view = RotationX((360.0f - verta360_) * kDegToRad);
    view = Multiply(BasisToward({ -look[0], -look[1], -look[2] }, up), view);
    view = Multiply(RotationY((360.0f - theta_) * kDegToRad), view);
    appliedView_ = { view.m[2][0], view.m[2][1], view.m[2][2] };
    appliedView2_ = { view.m[1][0], view.m[1][1], view.m[1][2] };
}

std::optional<ControlConfig> ParseControlStyle(std::string_view text) {
    if (text.size() != 3 || text[1] != '.' || (text[0] != '1' && text[0] != '2') || text[2] < '1' || text[2] > '4') {
        return std::nullopt;
    }
    return static_cast<ControlConfig>((text[0] - '1') * 4 + (text[2] - '1'));
}

PlayerOptions PlayerOptions::FromSaveBits(uint16_t bits) {
    // OPTION_INVERTLOOK, OPTION_AUTOAIM, OPTION_AIMCONTROL, OPTION_SIGHTONSCREEN,
    // OPTION_LOOKAHEAD, OPTION_DISPLAYAMMO, OPTION_SCREENWIDE, OPTION_SCREENRATIO,
    // OPTION_CONTROLTYPE and OPTION_SCREENCINEMA.
    PlayerOptions options;
    options.controlType = static_cast<ControlConfig>((bits & 0x0700) >> 8);
    options.lookInverted = (bits & 0x0001) != 0;
    options.autoAim = (bits & 0x0002) != 0;
    options.aimToggle = (bits & 0x0004) != 0;
    options.sightOnScreen = (bits & 0x0008) != 0;
    options.lookAhead = (bits & 0x0010) != 0;
    options.ammoOnScreen = (bits & 0x0020) != 0;
    if ((bits & 0x0800) != 0) {
        options.screen = ScreenSize::Cinema;
    } else if ((bits & 0x0040) != 0) {
        options.screen = ScreenSize::Wide;
    }
    options.ratio16x9 = (bits & 0x0080) != 0;
    return options;
}

void PlayerMove::StopTankSounds() {
    for (AudioDriver::SoundState* sound : tankSound_) {
        if (sound != nullptr && Game::Sound::GetPlayingState(sound) != 0) {
            Game::Sound::Deactivate(sound);
        }
    }
}

void PlayerMove::DetachTankSounds() {
    for (AudioDriver::SoundState*& sound : tankSound_) {
        Game::Sound::DetachHandle(&sound);
        sound = nullptr;
    }
}

void PlayerMove::Init(const PlayerSpawn& spawn, const Stan& stan) {
    DetachTankSounds();
    HeadMotion* head = head_;
    *this = PlayerMove{};
    head_ = head;

    theta_ = spawn.theta;
    thetaTransform_ = spawn.look;
    stanHeight_ = spawn.stanHeight;
    stanHeightFilter_ = spawn.stanHeightFilter;
    stanHeightRemainder_ = spawn.stanHeightRemainder;
    eyeHeight_ = spawn.eyeHeight;
    // field_78 and field_80 start at 0, which `*this = PlayerMove{}` gives.
    verticalSpeed_ = -0.0001f;

    // change_player_pos_to_target.
    appliedView_ = { 1.0f, 0.0f, 0.0f };
    appliedView2_ = { 0.0f, 1.0f, 0.0f };
    collisionRadius_ = 30.0f;
    collisionPosition_ = spawn.position;
    position_ = spawn.position;
    portalPosition_ = spawn.position;
    previousPosition_ = spawn.position;
    tile_ = spawn.stanTile;
    portalTile_ = spawn.stanTile;
    room_ = tile_ == kNoTile ? 0 : stan.TileRoom(tile_);
    portalRoom_ = room_;
    for (int axis = 0; axis < 3; axis++) {
        smoothedPosition_[axis] = position_[axis] / kSmoothedPositionInitial;
    }
    smoothedScaled_ = { 0.0f, 0.0f, 1.0f };
}

void PlayerMove::BeginLife(const PlayerSpawn& spawn, const Stan& stan) {
    // The prop and the body are the player's for the whole stage. Only
    // single-player stages have tanks, so the tank globals need no carrying.
    PropList* props = props_;
    const int prop = prop_;
    BodyHooks body = std::move(body_);
    const bool collides = propCollides_;
    Init(spawn, stan);
    props_ = props;
    prop_ = prop;
    body_ = std::move(body);
    propCollides_ = collides;
}

void PlayerMove::AdjustCrouch(int delta) {
    crouchPosition_ += delta;
    if (crouchPosition_ < kCrouchSquat) {
        crouchPosition_ = kCrouchSquat;
    } else if (crouchPosition_ > kCrouchStand) {
        crouchPosition_ = kCrouchStand;
    }
}

void PlayerMove::UpdateSpeedSideways(int direction) {
    const float delta = context_.globalTimerDelta;
    if (direction == -1) {
        speedStrafe_ = speedStrafe_ - delta;
        if (speedStrafe_ < -1.0f) {
            speedStrafe_ = -1.0f;
        }
    } else if (direction == 1) {
        speedStrafe_ = speedStrafe_ + delta;
        if (1.0f < speedStrafe_) {
            speedStrafe_ = 1.0f;
        }
    } else if (0.0f < speedStrafe_) {
        speedStrafe_ = speedStrafe_ - delta;
        if (speedStrafe_ < 0.0f) {
            speedStrafe_ = 0.0f;
        }
    } else {
        speedStrafe_ = speedStrafe_ + delta;
        if (0.0f < speedStrafe_) {
            speedStrafe_ = 0.0f;
        }
    }
    speedSideways_ = speedStrafe_;
}

void PlayerMove::UpdateSpeedForwards(int direction) {
    const float delta = context_.globalTimerDelta;
    if (direction == 1) {
        speedGo_ = speedGo_ + delta;
        if (1.0f < speedGo_) {
            speedGo_ = 1.0f;
        }
    } else if (direction == -1) {
        speedGo_ = speedGo_ - delta;
        if (speedGo_ < -1.0f) {
            speedGo_ = -1.0f;
        }
    } else if (0.0f < speedGo_) {
        speedGo_ = speedGo_ - delta;
        if (speedGo_ < 0.0f) {
            speedGo_ = 0.0f;
        }
    } else {
        speedGo_ = speedGo_ + delta;
        if (0.0f < speedGo_) {
            speedGo_ = 0.0f;
        }
    }
    speedForwards_ = speedGo_;
}

void PlayerMove::UpdateSpeedVerta(float value) {
    const float multiplier = context_.fovY / kFovY;
    const float limit = TurnLimit(context_.fovY, value);
    const float delta = context_.globalTimerDelta;

    if (value > 0.0f) {
        if (speedVerta_ > 0.0f) {
            speedVerta_ -= (0.05f * delta * multiplier);
        } else {
            speedVerta_ -= (0.0125f * delta * multiplier);
        }
        if (speedVerta_ < limit) {
            speedVerta_ = limit;
        }
    } else if (value < 0.0f) {
        if (speedVerta_ < 0.0f) {
            speedVerta_ += (0.05f * delta * multiplier);
        } else {
            speedVerta_ += (0.0125f * delta * multiplier);
        }
        if (speedVerta_ > limit) {
            speedVerta_ = limit;
        }
    } else if (speedVerta_ > limit) {
        speedVerta_ -= (0.05f * delta * multiplier);
        if (speedVerta_ < limit) {
            speedVerta_ = limit;
        }
    } else {
        speedVerta_ += (0.05f * delta * multiplier);
        if (speedVerta_ > limit) {
            speedVerta_ = limit;
        }
    }
}

void PlayerMove::UpdateSpeedTheta(float value) {
    const float multiplier = context_.fovY / kFovY;
    const float limit = TurnLimit(context_.fovY, value);
    const float delta = context_.globalTimerDelta;

    if (value > 0.0f) {
        if (speedTheta_ > 0.0f) {
            speedTheta_ -= (0.05f * delta * multiplier);
        } else {
            speedTheta_ -= (0.0125f * delta * multiplier);
        }
        if (speedTheta_ < limit) {
            speedTheta_ = limit;
        }
    } else if (value < 0.0f) {
        if (speedTheta_ < 0) {
            speedTheta_ += (0.05f * delta * multiplier);
        } else {
            speedTheta_ += (0.0125f * delta * multiplier);
        }
        if (limit < speedTheta_) {
            speedTheta_ = limit;
        }
    } else if (limit < speedTheta_) {
        speedTheta_ -= (0.05f * delta * multiplier);
        if (speedTheta_ < limit) {
            speedTheta_ = limit;
        }
    } else {
        speedTheta_ += (0.05f * delta * multiplier);
        if (limit < speedTheta_) {
            speedTheta_ = limit;
        }
    }
}

void PlayerMove::ApplyVertaTheta() {
    while (verta_ < -180.0f) {
        verta_ += 360.0f;
    }
    while (verta_ >= 180.0f) {
        verta_ -= 360.0f;
    }
    if (verta_ > 90.0f) {
        verta_ = 90.0f;
    } else if (verta_ < -90.0f) {
        verta_ = -90.0f;
    }

    cosTheta_ = GameCosf(theta_ * kDegToRad);
    sinTheta_ = GameSinf(theta_ * kDegToRad);

    verta360_ = verta_;
    if (verta360_ < 0.0f) {
        verta360_ += 360.0f;
    }
    cosVerta_ = GameCosf(verta360_ * kDegToRad);
    sinVerta_ = GameSinf(verta360_ * kDegToRad);

    thetaTransform_ = { -sinTheta_, 0.0f, cosTheta_ };
}

void PlayerMove::ApplySpeed(float* position, float target, float* speedPointer, float accel, float decel,
                            float maxSpeed) {
    ChrObjApplySpeed(position, target, speedPointer, accel, decel, maxSpeed, context_.clockTimer);
}

void PlayerMove::CollisionRadius(float* radius, float* height, float* always30) const {
    *radius = collisionRadius_;
    *height = (DuckingHeightRelated() + 10.0f) - 30.0f;
    *always30 = 30.0f;
}

float PlayerMove::FloorHeight(const Stan& stan, int tile, float x, float z) const {
    if (playerTank_ >= 0 && props_ != nullptr) {
        return TankFloor(stan);
    }
    if (onLadder_) {
        return stan.PositionY(ladderTile_, x, z);
    }
    return stan.PositionY(tile, x, z);
}

void PlayerMove::ProcessInput(const MoveContext& context, const MoveInput& input, Stan& stan) {
    const float delta = context.globalTimerDelta;
    const int stickX = input.stickX;
    const int stickY = input.stickY;
    const uint16_t buttons = input.buttons;
    const uint16_t oldButtons = buttonsPressed_;
    // A Modern port reads as control style 1.1 whatever the player's option
    // says, and has no invert-Y (see MoveInput::modern).
    const bool modern = input.modern;
    const ControlConfig control = modern ? ControlConfig::Honey : context.options.controlType;

    MoveData m;
    m.invertPitch = modern || !context.options.lookInverted;
    m.controlStickXSafe = SafeStick(stickX);
    m.controlStickYSafe = SafeStick(stickY);
    m.controlStickXRaw = stickX;
    m.controlStickYRaw = stickY;
    m.analogTurn = m.controlStickXSafe;
    m.analogPitch = m.controlStickYSafe;
    m.analogStrafe = m.controlStickXSafe;
    m.analogWalk = m.controlStickYSafe;

    // A Start press opens or closes the watch, and the controls below are only
    // read while it is closed and the player alive.
    WatchHooks* watch = context.watch;
    if (watch != nullptr) {
        watch->OnInput(((buttons & ~oldButtons) & Buttons::Start) != 0,
                       context.dead || context.forceDisarm || context.multiplayer);
    }
    const bool watchClosed = watch == nullptr || watch->State() == 0;
    const bool controlsLive = watchClosed && !context.dead && !context.padStopped;

    const bool twoControllers = control == ControlConfig::Plenty || control == ControlConfig::Galore ||
                                control == ControlConfig::Domino || control == ControlConfig::Goodhead;
    if (controlsLive && twoControllers) {
        const int stick2X = input.stick2X;
        const int stick2Y = input.stick2Y;
        const uint16_t buttons2 = input.buttons2;
        const uint16_t previousButtons2 = prevButtons2_;

        const int adjustedX = SafeStick(stick2X);
        const int adjustedY = SafeStick(stick2Y);

        // 2.1 and 2.3
        if (control == ControlConfig::Plenty || control == ControlConfig::Domino) {
            m.analogStrafe = adjustedX;
            m.analogPitch = adjustedY;
        } else {
            if (inTank_ && !inSightAim_) {
                m.analogTurn = adjustedX;
            } else {
                m.analogStrafe = adjustedX;
            }
            m.analogWalk = adjustedY;
        }

        bool aimHeld;
        bool aimPressed;
        bool shootHeld;
        bool shootPressed;
        // 2.1 and 2.2
        if (control == ControlConfig::Plenty || control == ControlConfig::Galore) {
            aimHeld = (buttons2 & Buttons::Z) != 0;
            aimPressed = ((buttons2 & ~previousButtons2) & Buttons::Z) != 0;
            shootHeld = (buttons & Buttons::Z) != 0;
            shootPressed = ((buttons & ~oldButtons) & Buttons::Z) != 0;
        } else {
            aimHeld = (buttons & Buttons::Z) != 0;
            aimPressed = ((buttons & ~oldButtons) & Buttons::Z) != 0;
            shootHeld = (buttons2 & Buttons::Z) != 0;
            shootPressed = ((buttons2 & ~previousButtons2) & Buttons::Z) != 0;
        }

        if (!context.controlsLocked && !context.actionsDisabled) {
            if (!context.options.aimToggle) {
                inSightAim_ = aimHeld;
            } else if (aimPressed) {
                inSightAim_ = !inSightAim_;
            }

            m.canSwivelGun = !inSightAim_;
            m.canAutoAim = !inSightAim_;
            m.canManualAim = inSightAim_;
            m.btap =
                (((buttons & ~oldButtons) & Buttons::B) != 0) || (((buttons2 & ~previousButtons2) & Buttons::B) != 0);
            m.canLookAhead = !inSightAim_;
            m.canTurnTank = true;
            m.canNaturalTurn = !inSightAim_;
            m.canNaturalPitch = !inSightAim_;

            if (inSightAim_ && stickY > kAimStickThreshold) {
                m.speedVertaDown = CapAtOne(static_cast<float>(stickY - kAimStickThreshold) / 10.0f);
            }
            if (inSightAim_ && stickY < -kAimStickThreshold) {
                m.speedVertaUp = CapAtOne(static_cast<float>(-kAimStickThreshold - stickY) / 10.0f);
            }
            if (inSightAim_ && stickX < -kAimStickThreshold) {
                m.aimTurnLeftSpeed = CapAtOne(static_cast<float>(-kAimStickThreshold - stickX) / 10.0f);
            }
            if (inSightAim_ && stickX > kAimStickThreshold) {
                m.aimTurnRightSpeed = CapAtOne(static_cast<float>(stickX - kAimStickThreshold) / 10.0f);
            }

            m.weaponBackOffset = (((buttons & Buttons::A) != 0) || ((buttons2 & Buttons::A) != 0)) && shootPressed;
            m.weaponForwardOffset = ((((buttons & ~oldButtons) & Buttons::A) != 0) ||
                                     (((buttons2 & ~previousButtons2) & Buttons::A) != 0)) &&
                                    !shootHeld;

            m.aiming = inSightAim_;
            m.zooming = inSightAim_;

            if (context.weaponDisablesCrouch && inSightAim_) {
                if (adjustedY < 0) {
                    m.zoomOutFovPersec = CapAtOne(static_cast<float>(-adjustedY) / 70.0f) * 2.0f;
                }
                if (adjustedY > 0) {
                    m.zoomInFovPersec = CapAtOne(static_cast<float>(adjustedY) / 70.0f) * 2.0f;
                }
            }

            m.crouchDown = !context.weaponDisablesCrouch && inSightAim_ && stick2Y < -30;
            m.crouchUp = !context.weaponDisablesCrouch && inSightAim_ && stick2Y > 30;

            // The remote mine's detonation chord: A and B together, on
            // either controller.
            const bool aHeld = (buttons & Buttons::A) != 0;
            const bool aPressed = ((buttons & ~oldButtons) & Buttons::A) != 0;
            const bool bHeld = (buttons & Buttons::B) != 0;
            const bool bPressed = ((buttons & ~oldButtons) & Buttons::B) != 0;
            const bool a2Held = (buttons2 & Buttons::A) != 0;
            const bool a2Pressed = ((buttons2 & ~previousButtons2) & Buttons::A) != 0;
            const bool b2Held = (buttons2 & Buttons::B) != 0;
            const bool b2Pressed = ((buttons2 & ~previousButtons2) & Buttons::B) != 0;
            if (((aHeld && bPressed) || (bHeld && aPressed) || (a2Held && b2Pressed) || (b2Held && a2Pressed)) &&
                context.weapons != nullptr && context.weapons->WeaponId(HandRight) == kItemRemoteMine) {
                m.detonating = true;
                m.weaponBackOffset = false;
                m.weaponForwardOffset = false;
                m.btap = false;
            }

            if (inTank_ && inSightAim_) {
                if (context.weapons != nullptr && context.weapons->WeaponId(HandRight) == kItemTankShells) {
                    m.controlStickXRaw = 0;
                    if (m.analogStrafe == 0) {
                        m.analogStrafe = m.analogTurn;
                    }
                } else if (m.analogStrafe == 0) {
                    if (m.aimTurnLeftSpeed > 0) {
                        m.tankTurnLeftSpeed = m.aimTurnLeftSpeed;
                    }
                    if (m.aimTurnRightSpeed > 0) {
                        m.tankTurnRightSpeed = m.aimTurnRightSpeed;
                    }
                }
                m.aimTurnLeftSpeed = 0;
                m.aimTurnRightSpeed = 0;
            }
        }

        m.triggerOn = shootHeld && (buttons & Buttons::A) == 0 && (buttons2 & Buttons::A) == 0;
        m.disableLookAhead = true;
        prevButtons2_ = buttons2;
    } else if (controlsLive) {
        uint16_t shootButtons;
        uint16_t aimButtons;
        uint16_t invButtons;
        // 1.3 and 1.4
        if (control == ControlConfig::Kissy || control == ControlConfig::Goodnight) {
            shootButtons = Buttons::A;
            aimButtons = Buttons::Z;
            invButtons = Buttons::L | Buttons::R;
        } else {
            shootButtons = Buttons::Z;
            aimButtons = Buttons::L | Buttons::R;
            invButtons = Buttons::A;
        }

        if (!context.controlsLocked && !context.actionsDisabled) {
            if (!context.options.aimToggle) {
                inSightAim_ = (buttons & aimButtons) != 0;
            } else if ((buttons & ~oldButtons) & aimButtons) {
                inSightAim_ = !inSightAim_;
            }

            m.canSwivelGun = !inSightAim_;
            m.canAutoAim = !inSightAim_;
            m.btap = ((buttons & ~oldButtons) & Buttons::B) != 0;
            m.canManualAim = inSightAim_;

            const uint16_t leftMask = Buttons::LeftPad | Buttons::LeftC;
            const uint16_t rightMask = Buttons::RightPad | Buttons::RightC;
            const uint16_t upMask = Buttons::UpPad | Buttons::UpC;
            const uint16_t downMask = Buttons::DownPad | Buttons::DownC;

            // 1.2 and 1.4
            if (control == ControlConfig::Solitare || control == ControlConfig::Goodnight) {
                if ((buttons & leftMask) != 0) {
                    if (!inSightAim_) {
                        if (inTank_) {
                            m.aimTurnLeftSpeed = 1.0f;
                        } else {
                            m.digitalStepLeft = true;
                        }
                    } else {
                        m.tankTurnLeftSpeed = 1.0f;
                    }
                }
                if ((buttons & rightMask) != 0) {
                    if (!inSightAim_) {
                        if (inTank_) {
                            m.aimTurnRightSpeed = 1.0f;
                        } else {
                            m.digitalStepRight = true;
                        }
                    } else {
                        m.tankTurnRightSpeed = 1.0f;
                    }
                }

                m.digitalStepForward = !inSightAim_ && (buttons & upMask) != 0;
                m.digitalStepBack = !inSightAim_ && (buttons & downMask) != 0;
                m.canNaturalPitch = !inSightAim_;
                if (inTank_) {
                    m.canTurnTank = !inSightAim_;
                } else {
                    m.canNaturalTurn = !inSightAim_;
                }
            } else {
                m.tankTurnLeftSpeed = (buttons & leftMask) != 0 ? 1.0f : 0.0f;
                m.tankTurnRightSpeed = (buttons & rightMask) != 0 ? 1.0f : 0.0f;
                m.digitalStepLeft = !inSightAim_ && (buttons & leftMask) != 0;
                m.digitalStepRight = !inSightAim_ && (buttons & rightMask) != 0;
                m.canLookAhead = !inSightAim_;

                if (!inSightAim_ && (buttons & upMask) != 0) {
                    m.speedVertaDown = 1.0f;
                }
                if (!inSightAim_ && (buttons & downMask) != 0) {
                    m.speedVertaUp = 1.0f;
                }
                m.canNaturalTurn = !inSightAim_;
            }

            if (inSightAim_ && stickY > kAimStickThreshold) {
                m.speedVertaDown = CapAtOne(static_cast<float>(stickY - kAimStickThreshold) / 10.0f);
            } else if (inSightAim_ && stickY < -kAimStickThreshold) {
                m.speedVertaUp = CapAtOne(static_cast<float>(-kAimStickThreshold - stickY) / 10.0f);
            }
            if (inSightAim_ && stickX < -kAimStickThreshold) {
                m.aimTurnLeftSpeed = CapAtOne(static_cast<float>(-kAimStickThreshold - stickX) / 10.0f);
            }
            if (inSightAim_ && stickX > kAimStickThreshold) {
                m.aimTurnRightSpeed = CapAtOne(static_cast<float>(stickX - kAimStickThreshold) / 10.0f);
            }

            m.weaponBackOffset = ((buttons & invButtons) != 0) && (((buttons & ~oldButtons) & shootButtons) != 0);
            m.weaponForwardOffset = (((buttons & ~oldButtons) & invButtons) != 0) && ((buttons & shootButtons) == 0);

            m.aiming = inSightAim_;
            m.zooming = inSightAim_;

            if (context.weaponDisablesCrouch && inSightAim_) {
                if ((buttons & downMask) != 0) {
                    m.zoomOutFovPersec = 1.0f;
                }
                if ((buttons & upMask) != 0) {
                    m.zoomInFovPersec = 1.0f;
                }
            }

            m.crouchDown = !context.weaponDisablesCrouch && inSightAim_ && (buttons & downMask) != 0;
            m.crouchUp = !context.weaponDisablesCrouch && inSightAim_ && (~buttons & upMask) != 0;
            m.rLeanLeft = inSightAim_ && (buttons & leftMask) != 0;
            m.rLeanRight = inSightAim_ && (buttons & rightMask) != 0;

            if ((((buttons & invButtons) != 0 && ((buttons & ~oldButtons) & Buttons::B) != 0) ||
                 ((buttons & Buttons::B) != 0 && ((buttons & ~oldButtons) & invButtons) != 0)) &&
                context.weapons != nullptr && context.weapons->WeaponId(HandRight) == kItemRemoteMine) {
                m.detonating = true;
                m.weaponBackOffset = false;
                m.weaponForwardOffset = false;
                m.btap = false;
            }

            if (inTank_ && inSightAim_) {
                if (context.weapons != nullptr && context.weapons->WeaponId(HandRight) == kItemTankShells) {
                    m.controlStickXRaw = 0;
                    m.canTurnTank = true;
                } else if (m.tankTurnLeftSpeed == 0 && m.tankTurnRightSpeed == 0) {
                    if (m.aimTurnLeftSpeed > 0) {
                        m.tankTurnLeftSpeed = m.aimTurnLeftSpeed;
                    }
                    if (m.aimTurnRightSpeed > 0) {
                        m.tankTurnRightSpeed = m.aimTurnRightSpeed;
                    }
                }
                m.aimTurnLeftSpeed = 0;
                m.aimTurnRightSpeed = 0;
            }
        }

        m.triggerOn = ((buttons & shootButtons) != 0) && ((buttons & invButtons) == 0);

        // 1.2 and 1.4
        if (control == ControlConfig::Solitare || control == ControlConfig::Goodnight) {
            m.disableLookAhead = true;
        }
    }

    // Modern movement and look (see MoveInput::modern). The style's stick is the
    // sight-aim crosshair; walking, strafing and the free look come from the
    // fields set for them, and none of that is live while aiming, so walking
    // stays off in sight-aim as in the decomp.
    const bool modernMove = modern && controlsLive && !context.controlsLocked && !context.actionsDisabled;
    float lookYawDegrees = 0.0f;
    float lookPitchDegrees = 0.0f;
    if (modernMove) {
        m.analogTurn = 0;
        m.analogPitch = 0;
        m.analogStrafe = 0;
        m.analogWalk = SafeStick(input.moveY);
        // The pitch is the player's own, not the slope ahead's.
        m.disableLookAhead = true;
        if (!m.detonating) {
            // B only ever carries the detonate chord: without a remote mine in
            // hand the chord must not change weapon or use a prop.
            m.btap = false;
            if (input.detonate) {
                m.weaponBackOffset = false;
                m.weaponForwardOffset = false;
            }
        }
        if (!inSightAim_) {
            if (inTank_) {
                m.analogTurn = SafeStick(input.moveX);
            } else {
                // The analog strafe the two-controller styles use.
                m.analogStrafe = SafeStick(input.moveX);
                m.canTurnTank = true;
            }
            const float fovScale = context_.fovY / kFovY;
            lookYawDegrees = (input.lookYaw + (input.lookYawRate * kFullStickDegreesPerTick * delta)) * fovScale;
            lookPitchDegrees = (input.lookPitch + (input.lookPitchRate * kFullStickDegreesPerTick * delta)) * fovScale;
        }
    }

    // Modern crouch replaces the decomp's aim-only crouchDown/crouchUp (see
    // MoveInput::crouchMode); the gates are the ones the decomp's block above
    // applies, plus a tank, where crouch is never adjusted.
    const bool crouchEdge = input.crouchHeld && !prevCrouchHeld_;
    prevCrouchHeld_ = input.crouchHeld;
    if (input.crouchMode != CrouchMode::Toggle || context.dead || context.weaponDisablesCrouch || inTank_) {
        crouchToggled_ = false;
    }
    if (input.crouchMode != CrouchMode::None) {
        m.crouchDown = false;
        m.crouchUp = false;
        if (controlsLive && !context.controlsLocked && !context.actionsDisabled && !context.weaponDisablesCrouch &&
            !inTank_) {
            if (input.crouchMode == CrouchMode::Toggle && crouchEdge) {
                crouchToggled_ = !crouchToggled_;
            }
            const bool crouch = input.crouchMode == CrouchMode::Toggle ? crouchToggled_ : input.crouchHeld;
            m.crouchDown = crouch;
            m.crouchUp = !crouch;
        }
    }

    // The Modern interact and reload requests pass the gates btap does; the
    // remote mine chord takes the tick instead.
    const bool modernLive = controlsLive && !context.controlsLocked && !context.actionsDisabled && !context.paused &&
                            !context.menuOn && !m.detonating;
    const bool interact = modernLive && input.interactPressed;

    usePressed_ = false;
    useReloads_ = false;
    reloadPressed_ = modernLive && input.reloadPressed && !inTank_;
    if (m.btap || interact) {
        if (inTank_ || (playerTank_ >= 0 && canEnterTank_ && context.tank != nullptr &&
                        context.tank->TankOf(playerTank_) != nullptr)) {
            UseTank();
        } else {
            usePressed_ = true;
            useReloads_ = m.btap;
        }
    }

    if (!m.invertPitch) {
        m.controlStickYRaw = -stickY;
        m.analogPitch = -m.analogPitch;
        const float down = m.speedVertaDown;
        m.speedVertaDown = m.speedVertaUp;
        m.speedVertaUp = down;
    }

    // The hit's display suppresses the trigger.
    if (context.damageShowing) {
        m.triggerOn = false;
    }
    PlayerWeapons* weapons = context.weapons;
    if (weapons != nullptr) {
        GunTickInput gun;
        gun.triggerOn = m.triggerOn;
        gun.clockTimer = context.clockTimer;
        gun.globalTimerDelta = context.globalTimerDelta;
        gun.controlsLocked = context.controlsLocked;
        gun.menuOn = context.menuOn;
        gun.paused = context.paused;
        gun.multiplayer = context.multiplayer;
        gun.aiming = m.aiming;
        gun.random = context.random;
        weapons->TickGameplay(gun, context.gunHooks);

        // Guards hear the loudest gun that fired.
        if (context.gunHooks != nullptr && (weapons->Firing(HandRight) || weapons->Firing(HandLeft))) {
            float noiseRadius = 0.0f;
            if (weapons->Firing(HandRight) && weapons->Noise(HandRight) > noiseRadius) {
                noiseRadius = weapons->Noise(HandRight);
            }
            if (weapons->Firing(HandLeft) && noiseRadius < weapons->Noise(HandLeft)) {
                noiseRadius = weapons->Noise(HandLeft);
            }
            context.gunHooks->GuardsHeardShot(noiseRadius);
        }
        // The crosshair's visibility follows `aiming`, which the stage reads
        // back with LastMove().
        if (m.zoomOutFovPersec > 0) {
            weapons->ZoomOut(m.zoomOutFovPersec);
        }
        if (m.zoomInFovPersec > 0) {
            weapons->ZoomIn(m.zoomInFovPersec);
        }
    }

    // The field of view follows the weapon's zoom while it is aimed.
    if (watchClosed && watch != nullptr) {
        float zoom = 60.0f;
        if (m.zooming && weapons != nullptr) {
            zoom = weapons->ItemZoom();
            if (zoom <= 0) {
                zoom = 60.0f;
            }
        }
        watch->UpdateZoom(zoom);
        // bondviewUpdateWatchZoomIn ends with viSetFovY(zoominfovy).
        context_.fovY = watch->ZoomFovY();
        if (context.setFovY) {
            context.setFovY(context_.fovY);
        }
    }

    if (inTank_) {
        tankTurretTurn_ = 0.0f;

        if (tankRunState_ == kTankRunning) {
            tankTurretTurn_ += lookYawDegrees * kDegToRad;
            if (m.tankTurnRightSpeed > 0) {
                tankTurretTurn_ += context.globalTimerDelta * m.tankTurnRightSpeed * kDegToRad;
            } else if (m.tankTurnLeftSpeed > 0) {
                tankTurretTurn_ -= context.globalTimerDelta * m.tankTurnLeftSpeed * kDegToRad;
            } else if (m.canTurnTank) {
                float targetSpeed = static_cast<float>(m.analogStrafe) / kStickRange;
                if (targetSpeed > 1.0f) {
                    targetSpeed = 1.0f;
                }
                if (targetSpeed < -1.0f) {
                    targetSpeed = -1.0f;
                }
                tankTurretTurn_ += kDegToRad * targetSpeed * context.globalTimerDelta;
            }

            if (!inSightAim_) {
                float forward = 0.0f;
                float rate = 1.0f;

                if (m.canLookAhead) {
                    forward = static_cast<float>(m.analogWalk) / kStickRange;
                } else if (m.digitalStepForward) {
                    forward = 1.0f;
                } else if (m.digitalStepBack) {
                    forward = -1.0f;
                }
                if (forward > 1.0f) {
                    forward = 1.0f;
                } else if (forward < -1.0f) {
                    forward = -1.0f;
                }

                float targetSpeed = forward * kTankMaxSpeed;
                if (tankDamagePenalty_ > 0) {
                    targetSpeed = targetSpeed * 0.5f;
                    rate = 4.0f;
                    tankDamagePenalty_ -= context.clockTimer;
                }

                if (targetSpeed != speedForwards_) {
                    if (speedForwards_ < targetSpeed) {
                        const float step =
                            ((((((targetSpeed - speedForwards_) / 4.0f) / kTankMaxSpeed) + 0.5f) * rate * 10.0f) /
                             60.0f);
                        speedForwards_ += step * context.globalTimerDelta;
                        if (targetSpeed < speedForwards_) {
                            speedForwards_ = targetSpeed;
                        }
                    } else if (targetSpeed < speedForwards_) {
                        const float step =
                            ((((((speedForwards_ - targetSpeed) / 4.0f) / kTankMaxSpeed) + 0.5f) * rate * -10.0f) /
                             60.0f);
                        speedForwards_ += step * context.globalTimerDelta;
                        if (speedForwards_ < targetSpeed) {
                            speedForwards_ = targetSpeed;
                        }
                    }
                }
            }
        }
    } else {
        if (m.digitalStepLeft) {
            UpdateSpeedSideways(-1);
        } else if (m.digitalStepRight) {
            UpdateSpeedSideways(1);
        } else {
            UpdateSpeedSideways(0);
        }

        if (m.canTurnTank) {
            speedSideways_ = static_cast<float>(m.analogStrafe) / kStickRange;
        }

        if (m.digitalStepForward) {
            UpdateSpeedForwards(1);
            speedMaxTime60_ += context.clockTimer;
        } else if (m.digitalStepBack) {
            UpdateSpeedForwards(-1);
        } else {
            UpdateSpeedForwards(0);
        }

        if (m.canLookAhead) {
            speedForwards_ = static_cast<float>(m.analogWalk) / kStickRange;
            if (m.analogWalk > kAimStickThreshold) {
                speedMaxTime60_ += context.clockTimer;
            } else {
                speedMaxTime60_ = 0;
            }
        }

        if (speedForwards_ > 1.0f) {
            speedForwards_ = 1;
        }
        if (speedForwards_ < -1.0f) {
            speedForwards_ = -1.0f;
        }
        if (speedSideways_ > 1) {
            speedSideways_ = 1;
        }
        if (speedSideways_ < -1) {
            speedSideways_ = -1;
        }

        speedForwards_ *= 1.08f;
        speedForwards_ *= speedBoost_;

        if (!m.canLookAhead && !m.digitalStepForward) {
            speedMaxTime60_ = 0;
        }

        if (m.rLeanLeft) {
            swayTarget_ = -1 * 75.0f;
        } else if (m.rLeanRight) {
            swayTarget_ = 1 * 75.0f;
        } else {
            swayTarget_ = 0 * 75.0f;
        }

        if (m.crouchDown) {
            AdjustCrouch(-2);
        } else if (m.crouchUp) {
            AdjustCrouch(2);
        }
    }

    if (speedMaxTime60_ >= kThreeSecondTicks) {
        if (speedBoost_ < kSpeedRunMax) {
            speedBoost_ += (kSpeedTickAdjust * context.globalTimerDelta);
        }
        if (speedBoost_ > kSpeedRunMax) {
            speedBoost_ = kSpeedRunMax;
        }
    } else {
        if (speedBoost_ > kSpeedRegularMax) {
            speedBoost_ -= (kSpeedTickAdjust * context.globalTimerDelta);
        }
        if (speedBoost_ < kSpeedRegularMax) {
            speedBoost_ = kSpeedRegularMax;
        }
    }
    // The view follows the ground ahead and the pitch stick only while the
    // watch is closed.
    if (watchClosed) {
        // By default the view is pitched slightly below the horizon.
        float targetPitch = -4.0f;

        // lookaheadcentreenabled is always set.
        if (lookAheadCentreEnabled_) {
            int aheadTile = tile_;
            float lookDistance = 300.0f;

            float radius;
            float height;
            float always30;
            CollisionRadius(&radius, &height, &always30);

            Vec3 ahead = { collisionPosition_[0] + (thetaTransform_[0] * 300.0f), collisionPosition_[1],
                           collisionPosition_[2] + (thetaTransform_[2] * 300.0f) };

            stan.ResetHits();
            if (stan.TestLineUnobstructed(&aheadTile, collisionPosition_[0], collisionPosition_[2], ahead[0], ahead[2],
                                          CdType::ClosedDoors, height, always30, 0.0f, 1.0f) != 0) {
                ahead[1] = FloorHeight(stan, aheadTile, ahead[0], ahead[2]);
            } else {
                ahead = stan.CollisionEdgeRayPoint(collisionPosition_, thetaTransform_);

                const float dx = ahead[0] - collisionPosition_[0];
                const float dz = ahead[2] - collisionPosition_[2];
                lookDistance = std::sqrt((dx * dx) + (dz * dz));
                ahead[1] = FloorHeight(stan, aheadTile, ahead[0], ahead[2]);
            }

            if (lookDistance > 0) {
                const float rise = ahead[1] - stanHeight_;
                if (rise > -300.0f && rise < 500.0f) {
                    targetPitch = ((GameAtan2f(rise, lookDistance) * 360.0f) / kTau) + -4.0f;
                    if (targetPitch >= 180.0f) {
                        targetPitch -= 360.0f;
                    }
                    if (targetPitch > 0) {
                        targetPitch *= 0.8666667f;
                    }
                }
            }
        }

        if (moveCentreRelease_ && m.analogWalk < 40 && m.analogWalk > -40) {
            moveCentreRelease_ = false;
        }

        if (!inTank_) {
            // Manual pitch input stops the automatic look ahead.
            if (m.speedVertaDown > 0 || m.speedVertaUp > 0 || lookPitchDegrees != 0.0f) {
                doCentreUpDown_ = false;
                prevUpDown_ = true;
                autoMoveCentre_ = false;
            } else {
                if (m.disableLookAhead) {
                    autoMoveCentre_ = false;
                } else if (autoMoveCentreEnabled_) {
                    if (m.canLookAhead && (m.analogWalk > kAimStickThreshold || m.analogWalk < -kAimStickThreshold)) {
                        autoMoveCentre_ = true;
                    }

                    // Look ahead starts when the pitch is 5 degrees above or 10 below
                    // the target.
                    if (autoMoveCentre_ && ((targetPitch + 5.0f) < verta_ || verta_ < (targetPitch + -10.0f)) &&
                        !moveCentreRelease_) {
                        doCentreUpDown_ = true;
                    }
                } else if (fastMoveCentreEnabled_ && m.canLookAhead &&
                           (m.analogWalk > kAimStickThreshold || m.analogWalk < -kAimStickThreshold) &&
                           ((targetPitch + 5.0f) < verta_ || verta_ < (targetPitch + -10.0f)) && !moveCentreRelease_) {
                    doCentreUpDown_ = true;
                }
                prevUpDown_ = false;
            }
        }

        if (doCentreUpDown_) {
            if (!inTank_) {
                float brake = (speedVerta_ * speedVerta_ * 0.5f) / 0.05f;

                if ((targetPitch + brake) < verta_) {
                    UpdateSpeedVerta(1.0f);
                } else if (verta_ < (targetPitch - brake)) {
                    UpdateSpeedVerta(-1.0f);
                } else {
                    UpdateSpeedVerta(0);
                }

                brake = verta_ + (2.0f * (speedVerta_ * delta));

                if (targetPitch < verta_ && targetPitch < brake) {
                    verta_ = brake;
                } else if (verta_ < targetPitch && brake < targetPitch) {
                    verta_ = brake;
                } else {
                    verta_ = targetPitch;
                    speedVerta_ = 0;
                    if (!prevUpDown_) {
                        doCentreUpDown_ = false;
                    }
                }
            }
        } else {
            if (modernMove && !inSightAim_) {
                // The mouse and right stick set the tick's pitch change directly,
                // with no easing. Looking up raises verta: the view's y is sin(verta).
                speedVerta_ = delta > 0.0f ? lookPitchDegrees / (delta * kVertaPerSpeedTick) : 0.0f;
            } else if (m.canNaturalPitch) {
                const float fovScale = context_.fovY / 60.0f;
                float pitch = static_cast<float>(m.analogPitch) / kStickRange;
                if (pitch > 1) {
                    pitch = 1;
                } else if (pitch < -1) {
                    pitch = -1;
                }
                pitch = pitch >= 0 ? pitch * pitch : pitch * -pitch;
                speedVerta_ = -pitch * fovScale;
            } else if (m.speedVertaDown > 0) {
                UpdateSpeedVerta(m.speedVertaDown);
                // Bug in the game: true for every walk value except exactly 60.
                if (m.canLookAhead && (m.analogWalk > 60 || m.analogWalk < 60)) {
                    moveCentreRelease_ = true;
                }
            } else if (m.speedVertaUp > 0) {
                UpdateSpeedVerta(-m.speedVertaUp);
                if (m.canLookAhead && (m.analogWalk > 60 || m.analogWalk < 60)) {
                    moveCentreRelease_ = true;
                }
            } else {
                UpdateSpeedVerta(0);
            }

            verta_ += speedVerta_ * delta * 3.5f;

            if (inTank_ && tankRunState_ == kTankRunning && verta_ < -20.0f) {
                verta_ = -20.0f;
            }
        }
    }

    if (m.canNaturalTurn) {
        float turn = static_cast<float>(m.analogTurn) / kStickRange;
        if (turn > 1) {
            turn = 1;
        } else if (turn < -1) {
            turn = -1;
        }
        turn = turn >= 0 ? turn * turn : turn * -turn;
        speedTheta_ = turn * (context_.fovY / kFovY);
    } else if (m.aimTurnLeftSpeed > 0) {
        UpdateSpeedTheta(m.aimTurnLeftSpeed);
    } else if (m.aimTurnRightSpeed > 0) {
        UpdateSpeedTheta(-m.aimTurnRightSpeed);
    } else {
        UpdateSpeedTheta(0);
    }
    if (modernMove && !inSightAim_ && !inTank_) {
        // The mouse and right stick turn the view by the tick's angle
        // directly; a tank turns its turret instead (above).
        speedTheta_ += delta > 0.0f ? lookYawDegrees / (delta * kVertaPerSpeedTick) : 0.0f;
    }

    if (inTank_) {
        if (tankRunState_ == kTankRunning) {
            float turn = 0.0f;
            if (m.canNaturalTurn) {
                turn = speedTheta_ * 0.3f;
            } else if (m.aimTurnLeftSpeed > 0) {
                turn = TurnLimit(context_.fovY, 1) * 0.3f;
            } else if (m.aimTurnRightSpeed > 0) {
                turn = TurnLimit(context_.fovY, -1) * 0.3f;
            }
            for (int i = 0; i < context.clockTimer; i++) {
                tankTurnSpeed_ = (kTankRotationScale * tankTurnSpeed_) + turn;
            }
            speedTheta_ = tankTurnSpeed_ * kTankVertAngleRadFactor;
        } else {
            speedTheta_ = 0;
        }
    }

    if (context.weapons != nullptr) {
        if (m.detonating) {
            context.weapons->DetonateRemoteMines();
        }
        if (context.canCycleWeapons) {
            if (m.weaponBackOffset) {
                context.weapons->BackstepThroughInventory();
            }
            if (m.weaponForwardOffset) {
                context.weapons->AdvanceThroughInventory();
            }
        }
    }
    if (m.canSwivelGun) {
        controlDef_ = ControlConfig::Honey;
    } else if (m.canManualAim) {
        controlDef_ = ControlConfig::Kissy;
    }

    if (context.weapons != nullptr) {
        if (controlDef_ == ControlConfig::Honey) {
            context.weapons->SetAimType(0);
            const bool autoAims =
                m.canAutoAim && context.autoAimEnabled && context.weapons->RightHandHasFlag(WeaponFlag::HasAutoAim);
            const float turnX = autoAims && context.autoAimTargetX ? context.autoAimX : speedTheta_ * 0.3f;
            const float turnY = autoAims && context.autoAimTargetY ? context.autoAimY : -speedVerta_ * 0.1f;
            context.weapons->AimSwivel(turnX, turnY, context.clockTimer, context.gunView);
        } else if (controlDef_ == ControlConfig::Kissy) {
            context.weapons->SetAimType(0);
            context.weapons->AimManual((static_cast<float>(m.controlStickXRaw) * 0.65f) / 80.0f,
                                       (static_cast<float>(m.controlStickYRaw) * 0.65f) / 80.0f, context.clockTimer,
                                       context.gunView);
        }
    }

    move_ = m;
}

bool PlayerMove::TryMoveToStan(Stan& stan, const Vec3& position, int* tile) {
    if (inTank_ && tankRunState_ != kTankNotRunning) {
        return TankTryMove(stan, position, tile);
    }

    bool moved = false;
    int probeTile = tile_;
    const int cdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;

    float radius;
    float height;
    float always30;
    CollisionRadius(&radius, &height, &always30);

    // The player's own prop is not in the way of its own move
    // (sub_GAME_7F03D058), nor is the tank it bumped into before.
    TankHooks* tank = context_.tank;
    if (worldTank_ >= 0 && tank != nullptr) {
        tank->SetCollides(worldTank_, false);
    }
    propCollides_ = false;
    const StanTileProbe probe = stan.TileDistance(probeTile, position[0], position[2], radius);
    if (probe.forceCrouch) {
        autoCrouchPosition_ = kCrouchSquat;
    }

    bool blocked = true;
    if (stan.TestLineUnobstructed(&probeTile, collisionPosition_[0], collisionPosition_[2], position[0], position[2],
                                  cdTypes, height, always30, 0.0f, 1.0f) != 0 &&
        stan.TestVolume(&probeTile, position[0], position[2], radius, cdTypes, height, always30) < 0) {
        if (duckingHeightOffset_ == kFullCrouchOffset || static_cast<int32_t>(probe.hit) < 0) {
            if (!probe.ladder &&
                static_cast<int32_t>(stan.TestLocusEdgeAboveY(probeTile, position[0], position[2], radius,
                                                              collisionPosition_[1] + 175.0f)) >= 0) {
                // Blocked by something overhead.
            } else {
                *tile = probeTile;
                moved = true;
                blocked = false;
            }
        }
    }
    if (blocked && playerTank_ < 0 && tank != nullptr) {
        // A blocked move against a tank makes it the world tank.
        const int hit = stan.LastCollision().prop;
        if (hit >= 0 && tank->TankOf(hit) != nullptr) {
            worldTank_ = hit;
        }
    }
    propCollides_ = true;
    if (worldTank_ >= 0 && tank != nullptr) {
        tank->SetCollides(worldTank_, true);
    }
    return moved;
}

bool PlayerMove::TrySimpleMove(Stan& stan, const Vec3& next, Vec3* pointA, Vec3* pointB) {
    stan.ResetHits();
    int tile = kNoTile;
    if (TryMoveToStan(stan, next, &tile)) {
        tile_ = tile;
        collisionPosition_[0] = next[0];
        collisionPosition_[2] = next[2];
        return true;
    }
    stan.CollisionEdge(pointA, pointB);
    return false;
}

int PlayerMove::TryFractionMove(Stan& stan, const Vec3& next, const Vec3& firstA, const Vec3& firstB, Vec3* pointA,
                                Vec3* pointB) {
    float radius;
    float height;
    float always30;
    CollisionRadius(&radius, &height, &always30);

    const Vec3 delta = { next[0] - collisionPosition_[0], 0.0f, next[2] - collisionPosition_[2] };
    const float fraction =
        CircleSegmentContactFraction(radius, { collisionPosition_[0], collisionPosition_[2] }, { firstA[0], firstA[2] },
                                     { firstB[0], firstB[2] }, { delta[0], delta[2] });
    const Vec3 attempt = { collisionPosition_[0] + (delta[0] * fraction * 0.25f), 0.0f,
                           collisionPosition_[2] + (delta[2] * fraction * 0.25f) };

    int tile = kNoTile;
    if (TryMoveToStan(stan, attempt, &tile)) {
        tile_ = tile;
        collisionPosition_[0] = attempt[0];
        collisionPosition_[2] = attempt[2];
        return 1;
    }

    stan.CollisionEdge(pointA, pointB);
    if ((*pointA)[0] != firstA[0] || (*pointA)[1] != firstA[1] || (*pointA)[2] != firstA[2] ||
        (*pointB)[0] != firstB[0] || (*pointB)[1] != firstB[1] || (*pointB)[2] != firstB[2]) {
        return 0;
    }
    return -1;
}

int PlayerMove::TryEdgeMove(Stan& stan, const Vec3& next, const Vec3& pointA, const Vec3& pointB) {
    const float deltaX = next[0] - collisionPosition_[0];
    const float deltaZ = next[2] - collisionPosition_[2];

    if (pointA[0] != pointB[0] || pointA[2] != pointB[2]) {
        float edgeX = pointB[0] - pointA[0];
        float edgeZ = pointB[2] - pointA[2];
        float scale = (edgeX * edgeX) + (edgeZ * edgeZ);
        scale = 1.0f / std::sqrt(scale);
        edgeX *= scale;
        edgeZ *= scale;

        // How far the move goes along the wall, and where that puts the player.
        scale = (deltaX * edgeX) + (deltaZ * edgeZ);
        const Vec3 attempt = { collisionPosition_[0] + (scale * edgeX), 0.0f, collisionPosition_[2] + (scale * edgeZ) };

        int tile = kNoTile;
        if (TryMoveToStan(stan, attempt, &tile)) {
            tile_ = tile;
            collisionPosition_[0] = attempt[0];
            collisionPosition_[2] = attempt[2];
            return 1;
        }
        return 0;
    }
    return -1;
}

bool PlayerMove::TryEndHopMove(Stan& stan, const Vec3& next, const Vec3& pointA, const Vec3& pointB) {
    float radius;
    float height;
    float always30;
    CollisionRadius(&radius, &height, &always30);

    const float deltaX = next[0] - collisionPosition_[0];
    const float deltaZ = next[2] - collisionPosition_[2];

    // Tries moving past the end `end` of the wall the player hit.
    auto hop = [&](const Vec3& end) {
        if (end[0] == collisionPosition_[0] && end[2] == collisionPosition_[2]) {
            return false;
        }
        float perpendicularX = -(end[2] - collisionPosition_[2]);
        float perpendicularZ = end[0] - collisionPosition_[0];
        float scale = (perpendicularX * perpendicularX) + (perpendicularZ * perpendicularZ);
        scale = 1.0f / std::sqrt(scale);
        perpendicularX *= scale;
        perpendicularZ *= scale;

        scale = (deltaX * perpendicularX) + (deltaZ * perpendicularZ);
        perpendicularX *= scale;
        perpendicularZ *= scale;
        const Vec3 attempt = { collisionPosition_[0] + perpendicularX, 0.0f, collisionPosition_[2] + perpendicularZ };

        int tile = kNoTile;
        if (TryMoveToStan(stan, attempt, &tile)) {
            tile_ = tile;
            collisionPosition_[0] = attempt[0];
            collisionPosition_[2] = attempt[2];
            return true;
        }
        return false;
    };

    float toX = pointA[0] - next[0];
    float toZ = pointA[2] - next[2];
    if (((toX * toX) + (toZ * toZ)) <= (radius * radius)) {
        if (hop(pointA)) {
            return true;
        }
    } else {
        toX = pointB[0] - next[0];
        toZ = pointB[2] - next[2];
        if (((toX * toX) + (toZ * toZ)) <= (radius * radius)) {
            if (hop(pointB)) {
                return true;
            }
        }
    }
    return false;
}

void PlayerMove::CalcUpdateCollision(Stan& stan, const Vec3& offset, bool allowScoot) {
    previousPosition_ = collisionPosition_;
    const Vec3 next = { collisionPosition_[0] + offset[0], 0.0f, collisionPosition_[2] + offset[2] };

    autoCrouchPosition_ = kCrouchStand;
    canEnterTank_ = false;
    if (worldTank_ >= 0 && context_.tank != nullptr && UpdateWorldTank(stan)) {
        // The player is still climbing onto the tank: it does not move this tick.
        return;
    }

    Vec3 firstA{};
    Vec3 firstB{};
    if (!TrySimpleMove(stan, next, &firstA, &firstB)) {
        // 1 when moved, 0 when it failed on a different edge, -1 when it failed
        // on the same one.
        Vec3 secondA{};
        Vec3 secondB{};
        const int fraction = TryFractionMove(stan, next, firstA, firstB, &secondA, &secondB);

        if (fraction != 0) {
            if (allowScoot && TryEdgeMove(stan, next, firstA, firstB) <= 0 &&
                !TryEndHopMove(stan, next, firstA, firstB)) {
                // Nothing to slide along.
            }
        } else {
            Vec3 thirdA{};
            Vec3 thirdB{};
            TryFractionMove(stan, next, secondA, secondB, &thirdA, &thirdB);

            if (allowScoot && TryEdgeMove(stan, next, secondA, secondB) <= 0 &&
                TryEdgeMove(stan, next, firstA, firstB) <= 0 && !TryEndHopMove(stan, next, secondA, secondB)) {
                TryEndHopMove(stan, next, firstA, firstB);
            }
        }
    }

    // Out of bounds of its own tile: step to a linked tile the position is in.
    if (!stan.PointWithinTileBounds(tile_, collisionPosition_[0], collisionPosition_[2])) {
        int candidate = tile_;
        for (int attempt = 0; attempt < 5; attempt++) {
            const StanTile& current = stan.Tile(candidate);
            uint32_t linked = 0;
            for (int i = 0; i < current.PointCount(); i++) {
                if (current.points[i].link != kNoTile) {
                    linked++;
                }
            }
            if (linked == 0) {
                break;
            }

            const uint32_t pick = (context_.random ? context_.random() : 0) % linked;
            uint32_t seen = 0;
            for (int i = 0; i < current.PointCount(); i++) {
                if (current.points[i].link != kNoTile) {
                    if (seen == pick) {
                        candidate = current.points[i].link;
                        break;
                    }
                    seen++;
                }
            }

            if (stan.PointWithinTileBounds(candidate, collisionPosition_[0], collisionPosition_[2])) {
                tile_ = candidate;
                break;
            }
        }
    }

    UpdateRoom(stan);
    // objectivestatusCheckRoomEntered runs from Stage::MovePlayer, which knows
    // the objectives.
}

void PlayerMove::UpdateRoom(const Stan& stan) {
    // bondviewUpdatePlayerRoom: a body takes the rooms its bounding box
    // reaches, the viewer alone the room of its tile.
    if (props_ != nullptr) {
        props_->DeregisterRoom(prop_, room_);
    }
    if (tile_ != kNoTile) {
        room_ = stan.TileRoom(tile_);
        if (props_ != nullptr) {
            if (body_.detectRooms) {
                body_.detectRooms();
            } else {
                props_->RegisterRoom(prop_, room_);
            }
        }
    }
}

void PlayerMove::AdoptBodyPosition(const Vec3& position, int tile, const Stan& stan) {
    collisionPosition_ = position;
    tile_ = tile;
    UpdateRoom(stan);
}

void PlayerMove::UpdatePlayerY(Stan& stan, bool useStanHeight, float stanHeightOffset) {
    const float delta = context_.globalTimerDelta;
    const float x = collisionPosition_[0];
    const float z = collisionPosition_[2];

    if (inTank_) {
        stanHeight_ = FloorHeight(stan, tile_, x, z);

        stanHeightFilter_ = stanHeightRemainder_ / (1.0f - kTankHeightScale);
        for (int i = 0; i < context_.clockTimer; i++) {
            stanHeightFilter_ = (stanHeightFilter_ * kTankHeightScale) + stanHeight_;
        }
        stanHeightRemainder_ = stanHeightFilter_ * (1.0f - kTankHeightScale);
    } else {
        if (useStanHeight) {
            stanHeight_ = stanHeight_ + stanHeightOffset;
            const float floor = FloorHeight(stan, tile_, x, z);
            if (stanHeight_ < floor) {
                stanHeight_ = floor;
            }
        } else {
            float radius;
            float height;
            float always30;
            CollisionRadius(&radius, &height, &always30);

            float floor = FloorHeight(stan, tile_, x, z);

            // An edge above the player's head raises the floor to where the
            // player already was.
            if (static_cast<int32_t>(stan.TestLocusEdgeAboveY(tile_, x, z, radius, DuckingHeightRelated() + floor)) >=
                0) {
                if (floor < stanHeight_) {
                    floor = stanHeight_;
                }
            }
            stanHeight_ = floor;
        }

        if (onLadder_ && stanHeightRemainder_ < stanHeight_) {
            onLadder_ = false;
            tile_ = ladderTile_;
            ladderTile_ = kNoTile;
        }

        if (verticalSpeed_ >= 0.0f || stanHeightRemainder_ < stanHeight_) {
            stanHeightFilter_ = stanHeightRemainder_ / (1.0f - kStanHeightScale);
            for (int i = 0; i < context_.clockTimer; i++) {
                stanHeightFilter_ = (stanHeightFilter_ * kStanHeightScale) + stanHeight_;
            }
            if (stanHeightRemainder_ < stanHeight_) {
                stanHeightRemainder_ = stanHeightFilter_ * (1.0f - kStanHeightScale);
            }
        }

        // Above the floor: fall.
        if (stanHeight_ < stanHeightRemainder_) {
            float newVerticalSpeed = verticalSpeed_;
            float newRemainder = stanHeightRemainder_;

            const bool forced = context_.forcedMove[0] != 0.0f || context_.forcedMove[2] != 0.0f;
            const float gravity = context_.fastBond && !forced ? 1.388889f : 0.27777779f;

            const float nextSpeed = newVerticalSpeed - (delta * gravity);
            newRemainder += (delta * (newVerticalSpeed + nextSpeed) * 0.5f);
            newVerticalSpeed = nextSpeed;

            if (newRemainder < stanHeight_) {
                newRemainder = stanHeight_;
                newVerticalSpeed =
                    -std::sqrt((verticalSpeed_ * verticalSpeed_) +
                               (((2.0f * (stanHeightRemainder_ - stanHeight_) * 0.27777779f) / 60.0f) * 60.0f));

                if (onLadder_) {
                    onLadder_ = false;
                    tile_ = ladderTile_;
                    ladderTile_ = kNoTile;
                }
            }

            stanHeightRemainder_ = newRemainder;
            verticalSpeed_ = newVerticalSpeed;
        }

        // Landed: bounce the view by how hard.
        if (verticalSpeed_ < 0.0f && stanHeightRemainder_ <= stanHeight_) {
            if (verticalSpeed_ < -13.333333f) {
                bounceTicks_ = kClippingBounceTicks;
                bounceAdjust_ = -90.0f;
            } else if (verticalSpeed_ < -5.0f) {
                bounceTicks_ = kClippingBounceTicks;
                bounceAdjust_ = ((-5.0f - verticalSpeed_) * -90.0f) / 8.333333f;
            }
            verticalSpeed_ = 0.0f;
        }

        if (onLadder_) {
            if (stanHeightRemainder_ + DuckingHeightRelated() < stan.PositionY(tile_, x, z)) {
                onLadder_ = false;
                tile_ = ladderTile_;
                ladderTile_ = kNoTile;
            }
        }
    }

    for (int i = 0; i < context_.clockTimer; i++) {
        if (bounceTicks_ > 0) {
            bounce_ = (bounce_ * kClippingClockFactor) + bounceAdjust_;
            bounceTicks_ += -1;
        } else {
            if (bounceAdjust_ < 0.0f) {
                bounceAdjust_ -= kClippingBounceDecay;
                if (0.0f <= bounceAdjust_) {
                    bounceAdjust_ = 0.0f;
                }
            }
            bounce_ = (bounce_ * kClippingClockFactor) + bounceAdjust_;
        }
    }

    bounceOffset_ = bounce_ * kClippingBounceFactor;
}

bool PlayerMove::Footprint(PropCollider* out) const {
    if (!propCollides_ || cameraFrozen_) {
        return false;
    }
    out->edges = 4;
    for (size_t i = 0; i < 4; i++) {
        out->polygon[i] = { collisionBounds_[i * 2], collisionBounds_[(i * 2) + 1] };
    }
    out->bottom = stanHeightRemainder_;
    out->top = out->bottom + DuckingHeightRelated() + 10.0f;
    return true;
}

void PlayerMove::UpdateCollisionPositionFields(Stan& stan) {
    const float perspective = context_.perspectiveHeight;

    eyeHeight_ = (headPosition_[1] * perspective) + kEyeOffset;

    float reach = eyeHeight_ + ((bounceOffset_ + duckingHeightOffset_) * perspective);
    if (reach < kMinimumEyeReach) {
        reach = kMinimumEyeReach;
    }

    collisionPosition_[1] = stanHeightRemainder_ + reach;

    // While the death camera or the outro shows Bond's body, the game keeps the
    // position of the body's head here instead. Nothing reads it then: the
    // camera is placed by the cinematic modes, and this field is rebuilt from
    // the collision position by every move.
    position_ = collisionPosition_;

    if (context_.dead && lookDownDrop_ > 0.0f) {
        lookDownDrop_ -= 0.25f;
        if (lookDownDrop_ < 0.0f) {
            lookDownDrop_ = 0.0f;
        }
    }
    if (verta_ < 0.0f) {
        position_[1] += -(1.0f - cosVerta_) * lookDownDrop_;
    }

    int portalTile = tile_;
    stan.WalkTiles(&portalTile, collisionPosition_[0], collisionPosition_[2], position_[0], position_[2]);
    portalTile_ = portalTile;
    portalRoom_ = portalTile != kNoTile ? stan.TileRoom(portalTile) : room_;

    portalPosition_ = { position_[0], FloorHeight(stan, portalTile_, position_[0], position_[2]), position_[2] };

    if (props_ != nullptr) {
        Prop& prop = (*props_)[prop_];
        prop.stan = tile_;
        prop.position = collisionPosition_;
    }
    for (int i = 0; i < context_.clockTimer; i++) {
        for (int axis = 0; axis < 3; axis++) {
            smoothedPosition_[axis] = (kSmoothedPositionScale * smoothedPosition_[axis]) + position_[axis];
        }
    }
    for (int axis = 0; axis < 3; axis++) {
        smoothedScaled_[axis] = smoothedPosition_[axis] * kSmoothedPositionFactor;
    }
}

void PlayerMove::RestoreDeathState(Stan& stan) {
    inTank_ = false;

    collisionPosition_ = deathState_.collisionPosition;
    position_ = deathState_.position;
    portalPosition_ = deathState_.portalPosition;
    tile_ = deathState_.tile;
    portalTile_ = deathState_.portalTile;
    portalRoom_ = deathState_.portalRoom;
    room_ = deathState_.room;
    thetaTransform_ = deathState_.thetaTransform;
    appliedView_ = deathState_.appliedView;
    appliedView2_ = deathState_.appliedView2;
    collisionRadius_ = deathState_.collisionRadius;

    theta_ = thetaDie_;
    verta_ = vertaDie_;
    if (props_ != nullptr) {
        Prop& prop = (*props_)[prop_];
        prop.position = collisionPosition_;
        prop.stan = tile_;
    }

    ApplyVertaTheta();
    // bondviewMoveAnimationTick(0, 0, 0).
    headPosition_ =
        head_ != nullptr ? head_->Tick(0.0f, 0.0f, 0.0f, breathing_, context_) : Vec3{ 0.0f, standHeight_, 0.0f };
    UpdateAppliedView(head_ != nullptr ? head_->Look() : Vec3{ 0.0f, 0.0f, 1.0f },
                      head_ != nullptr ? head_->Up() : Vec3{ 0.0f, 1.0f, 0.0f });
    UpdateCollisionPositionFields(stan);
}

void PlayerMove::TickFrozen(const MoveContext& context, const MoveInput& input, Stan& stan) {
    context_ = context;
    const float delta = context.globalTimerDelta;

    // bondviewPlayerTickDamageAndHealth and bondviewPlayerTickExplode run in
    // Stage::MovePlayer ahead of this.
    const bool crouchToggled = crouchToggled_;
    ProcessInput(context, MoveInput{}, stan);
    // The empty input read the crouch key as released and the mode as unset: a
    // key still held when the player can move again is not a new press, and a
    // toggled crouch is not cancelled by the cinematic.
    prevCrouchHeld_ = input.crouchHeld;
    crouchToggled_ = crouchToggled;
    ApplyVertaTheta();

    // bondviewMoveAnimationTick(0, 0, 0).
    headPosition_ =
        head_ != nullptr ? head_->Tick(0.0f, 0.0f, 0.0f, breathing_, context) : Vec3{ 0.0f, standHeight_, 0.0f };
    UpdateAppliedView(head_ != nullptr ? head_->Look() : Vec3{ 0.0f, 0.0f, 1.0f },
                      head_ != nullptr ? head_->Up() : Vec3{ 0.0f, 1.0f, 0.0f });

    Vec3 moveOffset = { 0.0f, 0.0f, 0.0f };
    if (context.forcedMove[0] != 0.0f || context.forcedMove[2] != 0.0f) {
        moveOffset[0] += context.forcedMove[0] * delta;
        moveOffset[2] += context.forcedMove[2] * delta;
    }
    const float headX = headPosition_[0];
    const float headZ = headPosition_[2];
    moveOffset[0] += ((headZ * thetaTransform_[0]) - (headX * thetaTransform_[2])) * delta;
    moveOffset[2] += ((headZ * thetaTransform_[2]) + (headX * thetaTransform_[0])) * delta;

    CalcUpdateCollision(stan, moveOffset, true);
    UpdatePlayerY(stan, false, 0.0f);
    UpdateCollisionPositionFields(stan);

    buttonsPressed_ = input.buttons;
}

void PlayerMove::Tick(const MoveContext& context, const MoveInput& input, Stan& stan) {
    context_ = context;
    const float delta = context.globalTimerDelta;

    Vec3 moveOffset = { 0.0f, 0.0f, 0.0f };
    bool useStanHeight = false;
    float maxSpeed = 0.0f;
    float stanHeightOffset = 0.0f;

    // g_bondviewForceDisarm, bondviewPlayerTickDamageAndHealth and
    // bondviewPlayerTickExplode run in Stage::MovePlayer ahead of this, which
    // also does currentPlayerSetCameraMode(0).
    ProcessInput(context, input, stan);

    if (context.controlsLocked && context.stopAudioForPause) {
        context.stopAudioForPause();
    }
    if (context.watch != nullptr && context.watch->State() != 0) {
        context.watch->Tick();
    }

    // Crouching halves the speed, and a shot pushes the player. A tank is not
    // boosted.
    if (!inTank_) {
        if (CrouchPosition() == kCrouchSquat) {
            speedForwards_ *= 0.5f;
            speedSideways_ *= 0.5f;
        }

        if (shotSpeed_[0] != 0.0f || shotSpeed_[2] != 0.0f) {
            const float boostForward = (-shotSpeed_[0] * sinTheta_) + (shotSpeed_[2] * cosTheta_);
            const float boostSideways = (-shotSpeed_[0] * cosTheta_) - (shotSpeed_[2] * sinTheta_);
            const float norm = std::sqrt((shotSpeed_[0] * shotSpeed_[0]) + (shotSpeed_[2] * shotSpeed_[2]));

            speedForwards_ += boostForward;
            speedSideways_ += boostSideways;

            for (int i = 0; i < 3; i++) {
                if (shotSpeed_[i] != 0.0f) {
                    if (shotSpeed_[i] > 0.0f) {
                        shotSpeed_[i] -= ((0.06666667f * delta * shotSpeed_[i]) / norm);
                        if (shotSpeed_[i] < 0.0f) {
                            shotSpeed_[i] = 0.0f;
                        }
                    } else if (shotSpeed_[i] < 0.0f) {
                        shotSpeed_[i] -= ((0.06666667f * delta * shotSpeed_[i]) / norm);
                        if (shotSpeed_[i] > 0.0f) {
                            shotSpeed_[i] = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // Turn: the tank turns its hull and turret instead.
    if (inTank_) {
        TurnTank(stan);
    } else {
        float turned = theta_ + (speedTheta_ * delta * 3.5f);
        while (turned < 0.0f) {
            turned += 360.0f;
        }
        while (turned >= 360.0f) {
            turned -= 360.0f;
        }
        theta_ = turned;
    }

    ApplyVertaTheta();

    // Crouching and the animation between standing and crouching.
    {
        float target = 0.0f;
        if (CrouchPosition() == kCrouchSquat) {
            target = kFullCrouchOffset;
        } else if (CrouchPosition() == kCrouchHalf) {
            target = kHalfCrouchOffset;
        }

        if (target != duckingHeightOffset_) {
            ApplySpeed(&duckingHeightOffset_, target, &duckingSpeed_, kChrObjAccel, kChrObjAccel, kChrObjMaxSpeed);
        }
        if (target == duckingHeightOffset_) {
            duckingSpeed_ = 0.0f;
        }
    }

    if (inTank_) {
        DriveTank(stan, &moveOffset);
    } else {
        // The engine fades out after the player steps out, and the tracks stop.
        if (tankSound_[0] != nullptr && Game::Sound::GetPlayingState(tankSound_[0]) != 0) {
            tankEngineVolume_ -= context_.clockTimer * 1000;
            if (tankEngineVolume_ > 0) {
                Game::Sound::SetVolume(tankSound_[0], tankEngineVolume_);
            } else {
                Game::Sound::Deactivate(tankSound_[0]);
            }
        }
        if (tankSound_[1] != nullptr && Game::Sound::GetPlayingState(tankSound_[1]) != 0) {
            Game::Sound::Deactivate(tankSound_[1]);
        }
        // Leaning slides the player sideways towards `swayTarget_`.
        const float swayX = -swayTarget_ * thetaTransform_[2];
        const float swayZ = swayTarget_ * thetaTransform_[0];

        float leanX = swayX - swayOffset0_;
        float leanZ = swayZ - swayOffset2_;
        const float leanDistance = (leanX * leanX) + (leanZ * leanZ);
        if (leanDistance >= 100.0f) {
            leanX *= 0.6f * 1.0f;
            leanZ *= 0.6f * 1.0f;
        }

        float speedSideways = speedSideways_ * kMaxSpeedFactor;
        float speedForwards = speedForwards_;
        float speedTheta = speedTheta_ * kMaxSpeedFactor;
        if (speedSideways < 0.0f) {
            speedSideways = -speedSideways;
        }
        if (speedForwards < 0.0f) {
            speedForwards = -speedForwards;
        }
        if (speedTheta < 0.0f) {
            speedTheta = -speedTheta;
        }

        maxSpeed = speedForwards;
        if (maxSpeed < speedSideways) {
            maxSpeed = speedSideways;
        }
        if (maxSpeed < speedTheta) {
            maxSpeed = speedTheta;
        }
        if (leanDistance >= 0.1f && maxSpeed < kMaxSpeedFactor) {
            maxSpeed = kMaxSpeedFactor;
        }

        if (maxSpeed >= 0.750f) {
            breathing_ += (maxSpeed - 0.750f) * delta / 900.0f;
        } else {
            breathing_ -= (0.750f - maxSpeed) * delta / 2700.0f;
        }
        if (breathing_ < 0.0f) {
            breathing_ = 0.0f;
        } else if (breathing_ > 1.0f) {
            breathing_ = 1.0f;
        }

        // bondviewMoveAnimationTick.
        headPosition_ = head_ != nullptr ? head_->Tick(maxSpeed, speedForwards_, speedSideways_, breathing_, context)
                                         : Vec3{ 0.0f, standHeight_, 0.0f };
        UpdateAppliedView(head_ != nullptr ? head_->Look() : Vec3{ 0.0f, 0.0f, 1.0f },
                          head_ != nullptr ? head_->Up() : Vec3{ 0.0f, 1.0f, 0.0f });

        const float headX = headPosition_[0];
        const float headZ = headPosition_[2];
        moveOffset[0] += ((headZ * thetaTransform_[0]) - (headX * thetaTransform_[2])) * delta;
        moveOffset[2] += ((headZ * thetaTransform_[2]) + (headX * thetaTransform_[0])) * delta;

        moveOffset[0] += leanX;
        moveOffset[2] += leanZ;

        const float startX = collisionPosition_[0];
        const float startZ = collisionPosition_[2];
        const int startTile = tile_;

        if (context_.fastBond) {
            moveOffset[0] +=
                ((thetaTransform_[0] * speedForwards_) - (thetaTransform_[2] * speedSideways_)) * delta * 10.0f;
            moveOffset[2] +=
                ((thetaTransform_[2] * speedForwards_) + (thetaTransform_[0] * speedSideways_)) * delta * 10.0f;
        }

        CalcUpdateCollision(stan, moveOffset, swayTarget_ == 0.0f);

        // Near a ladder tile: use the tile floor heights and climb.
        StanTileProbe locus = stan.TileDistance(startTile, startX, startZ, collisionRadius_ * 1.16f);
        if (locus.ladder) {
            useStanHeight = true;
        }

        locus = stan.TileDistance(tile_, collisionPosition_[0], collisionPosition_[2], collisionRadius_ * 1.01f);
        if (locus.ladder) {
            useStanHeight = true;
        }

        locus = stan.TileDistance(tile_, collisionPosition_[0], collisionPosition_[2], collisionRadius_);
        if (!locus.ladder) {
            locus = stan.TileDistance(startTile, startX, startZ, collisionRadius_ * 0.990099f);
        }

        if (locus.ladder) {
            useStanHeight = true;
            int tile1 = kNoTile;
            int tile2 = kNoTile;
            std::array<Vec3, 4> ladder{};
            stan.GetLadderCollision(&tile1, &tile2, &ladder);

            if (stanHeight_ <= ladder[2][1]) {
                float normalX = ladder[0][0] - ladder[1][0];
                float normalZ = ladder[1][2] - ladder[0][2];
                const float length = std::sqrt((normalZ * normalZ) + (normalX * normalX));
                normalX = normalX / length;
                normalZ = normalZ / length;

                float from = ((startX - ladder[0][0]) * normalZ) + ((startZ - ladder[0][2]) * normalX);
                float to = (((startX + moveOffset[0]) - ladder[0][0]) * normalZ) +
                           (((startZ + moveOffset[2]) - ladder[0][2]) * normalX);

                float shorten;
                if ((from * to) <= 0.0f) {
                    shorten = 0.0f;
                } else {
                    if (to < 0.0f) {
                        from = -from;
                        to = -to;
                    }

                    if (from <= to) {
                        shorten = 0.0f;
                    } else if (collisionRadius_ < to) {
                        shorten = 0.0f;
                    } else if (from < collisionRadius_) {
                        shorten = 0.0f;
                    } else {
                        shorten = (from - collisionRadius_) / (from - to);
                    }
                }

                const float moveLength = std::sqrt((moveOffset[0] * moveOffset[0]) + (moveOffset[2] * moveOffset[2]));
                if (moveLength > 0.0f) {
                    float remaining = (1.0f - shorten) * moveLength;
                    stanHeightOffset = remaining * 0.25f;

                    if (ladder[2][1] <= (stanHeight_ + stanHeightOffset)) {
                        stanHeightOffset = (ladder[2][1] - stanHeight_);
                        remaining -= (stanHeightOffset / 0.25f);
                        shorten += (remaining / moveLength);
                    }
                }

                float alongX = ladder[2][0] - ladder[0][0];
                float alongZ = ladder[2][2] - ladder[0][2];
                const float otherX = ladder[3][0] - ladder[0][0];
                const float otherZ = ladder[3][2] - ladder[0][2];
                if (((otherX * otherX) + (otherZ * otherZ)) < ((alongX * alongX) + (alongZ * alongZ))) {
                    alongX = otherX;
                    alongZ = otherZ;
                }

                moveOffset[0] =
                    (moveOffset[0] * shorten) + (alongX * (stanHeightOffset / (ladder[2][1] - ladder[0][1])));
                moveOffset[2] =
                    (moveOffset[2] * shorten) + (alongZ * (stanHeightOffset / (ladder[2][1] - ladder[0][1])));

                collisionPosition_[0] = startX;
                collisionPosition_[2] = startZ;
                tile_ = startTile;

                CalcUpdateCollision(stan, moveOffset, swayTarget_ == 0.0f);
            }
        }

        // The lean moves the player only as far as it got.
        const float movedX = collisionPosition_[0] - startX;
        const float movedZ = collisionPosition_[2] - startZ;
        float moved = (moveOffset[0] * moveOffset[0]) + (moveOffset[2] * moveOffset[2]);
        if (moved != 0.0f) {
            moved = ((movedX * movedX) + (movedZ * movedZ)) / moved;
        }
        moved = std::sqrt(moved);
        swayOffset0_ += moved * leanX;
        swayOffset2_ += moved * leanZ;
    }

    if (context.weapons != nullptr) {
        float weaponSpeedVerta = (speedVerta_ / 0.7f) + (duckingSpeed_ / kChrObjMaxSpeed);
        float headBreathing = head_ != nullptr ? head_->BreathingValue(breathing_) : 0.0f;
        if (weaponSpeedVerta > 1.0f) {
            weaponSpeedVerta = 1.0f;
        } else if (weaponSpeedVerta < -1.0f) {
            weaponSpeedVerta = -1.0f;
        }
        if (head_ != nullptr && head_->OnFastAnimation()) {
            headBreathing *= 1.2f;
        }
        context.weapons->SetBondWeaponSway(headBreathing, breathing_, maxSpeed, weaponSpeedVerta, speedTheta_,
                                           context.clockTimer, context.globalTimerDelta, context.random);
        context.weapons->SetOffsetRelated(verta360_ * 0.017453292519943295f);
    }
    if (playerTank_ >= 0 && inTank_ && tankRunState_ == kTankRunning) {
        TickTankProp();
    }

    UpdatePlayerY(stan, useStanHeight, stanHeightOffset);
    UpdateCollisionPositionFields(stan);

    // bondviewUpdatePlayerCollisionBounds.
    if (inTank_) {
        const std::array<Vec2, 8> hull = TankBounds(collisionPosition_, tankOrientation_);
        for (size_t i = 0; i < 4; i++) {
            collisionBounds_[i * 2] = hull[i][0];
            collisionBounds_[(i * 2) + 1] = hull[i][1];
        }
    } else {
        collisionBounds_ = { collisionPosition_[0] + collisionRadius_,
                             collisionPosition_[2],
                             collisionPosition_[0],
                             collisionPosition_[2] + collisionRadius_,
                             collisionPosition_[0] - collisionRadius_,
                             collisionPosition_[2],
                             collisionPosition_[0],
                             collisionPosition_[2] - collisionRadius_ };
    }

    buttonsPressed_ = input.buttons;
}

} // namespace Frigate::Level
