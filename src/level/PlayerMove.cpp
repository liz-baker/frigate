#include "level/PlayerMove.h"

#include <cmath>

#include "level/Matrix.h"
#include "level/PropList.h"
#include "level/StanIntersection.h"

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

void PlayerMove::Init(const PlayerSpawn& spawn, const Stan& stan) {
    HeadMotion* head = head_;
    *this = PlayerMove{};
    head_ = head;

    theta_ = spawn.theta;
    thetaTransform_ = spawn.look;
    stanHeight_ = spawn.stanHeight;
    stanHeightFilter_ = spawn.stanHeightFilter;
    stanHeightRemainder_ = spawn.stanHeightRemainder;
    eyeHeight_ = spawn.eyeHeight;

    // change_player_pos_to_target.
    collisionPosition_ = spawn.position;
    position_ = spawn.position;
    portalPosition_ = spawn.position;
    previousPosition_ = spawn.position;
    tile_ = spawn.stanTile;
    portalTile_ = spawn.stanTile;
    room_ = tile_ == kNoTile ? 0 : stan.TileRoom(tile_);
    for (int axis = 0; axis < 3; axis++) {
        smoothedPosition_[axis] = position_[axis] / kSmoothedPositionInitial;
    }
    smoothedScaled_ = { 0.0f, 0.0f, 1.0f };
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

    cosTheta_ = std::cos(theta_ * kDegToRad);
    sinTheta_ = std::sin(theta_ * kDegToRad);

    verta360_ = verta_;
    if (verta360_ < 0.0f) {
        verta360_ += 360.0f;
    }
    cosVerta_ = std::cos(verta360_ * kDegToRad);
    sinVerta_ = std::sin(verta360_ * kDegToRad);

    thetaTransform_ = { -sinTheta_, 0.0f, cosTheta_ };
}

void PlayerMove::ApplySpeed(float* position, float target, float* speedPointer, float accel, float decel,
                            float maxSpeed) {
    float speed = *speedPointer;
    for (int i = 0; i < context_.clockTimer; i++) {
        const float limit = speed * speed * 0.5f / decel;
        const float remaining = target - *position;
        if (remaining > 0.0f) {
            if (speed > 0.0f && remaining <= limit) {
                speed -= decel;
                if (speed < decel) {
                    speed = decel;
                }
            } else if (speed < maxSpeed) {
                if (speed < 0.0f) {
                    speed += decel;
                } else {
                    speed += accel;
                }
                if (speed > maxSpeed) {
                    speed = maxSpeed;
                }
            }

            if (speed >= remaining) {
                *position = target;
                break;
            }
            *position += speed;
        } else {
            if (speed < 0.0f && -remaining <= limit) {
                speed += decel;
                if (speed > -decel) {
                    speed = -decel;
                }
            } else if (speed > -maxSpeed) {
                if (speed > 0.0f) {
                    speed -= decel;
                } else {
                    speed -= accel;
                }
                if (speed < -maxSpeed) {
                    speed = -maxSpeed;
                }
            }

            if (speed <= remaining) {
                *position = target;
                break;
            }
            *position += speed;
        }
    }
    *speedPointer = speed;
}

void PlayerMove::CollisionRadius(float* radius, float* height, float* always30) const {
    *radius = collisionRadius_;
    *height = (DuckingHeightRelated() + 10.0f) - 30.0f;
    *always30 = 30.0f;
}

float PlayerMove::FloorHeight(const Stan& stan, int tile, float x, float z) const {
    // TODO(https://github.com/liz-baker/frigate/issues/148): the tank prop's
    // floor plus its offset while the player is in or on a tank.
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
    const ControlConfig control = context.options.controlType;

    MoveData m;
    m.invertPitch = !context.options.lookInverted;
    m.controlStickXSafe = SafeStick(stickX);
    m.controlStickYSafe = SafeStick(stickY);
    m.controlStickXRaw = stickX;
    m.controlStickYRaw = stickY;
    m.analogTurn = m.controlStickXSafe;
    m.analogPitch = m.controlStickYSafe;
    m.analogStrafe = m.controlStickXSafe;
    m.analogWalk = m.controlStickYSafe;

    // TODO(https://github.com/liz-baker/frigate/issues/147): START opens the
    // watch (trigger_solo_watch_menu).
    // TODO(https://github.com/liz-baker/frigate/issues/146): a dead player
    // takes no input.

    const bool twoControllers = control == ControlConfig::Plenty || control == ControlConfig::Galore ||
                                control == ControlConfig::Domino || control == ControlConfig::Goodhead;
    if (twoControllers) {
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
            // TODO(https://github.com/liz-baker/frigate/issues/148): in a tank
            // the stick turns rather than strafes.
            m.analogStrafe = adjustedX;
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

        // disablePlayerActionsWhenPausedOrInMpMenu is true with one player.
        if (!context.controlsLocked) {
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

            // TODO(https://github.com/liz-baker/frigate/issues/153): the
            // remote mine's detonation chord.
            // TODO(https://github.com/liz-baker/frigate/issues/148): the tank
            // turret controls of aim mode.
        }

        m.triggerOn = shootHeld && (buttons & Buttons::A) == 0 && (buttons2 & Buttons::A) == 0;
        m.disableLookAhead = true;
        prevButtons2_ = buttons2;
    } else {
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

        if (!context.controlsLocked) {
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
                // TODO(https://github.com/liz-baker/frigate/issues/148): in a
                // tank the pad turns the turret instead of stepping.
                if ((buttons & leftMask) != 0) {
                    if (!inSightAim_) {
                        m.digitalStepLeft = true;
                    } else {
                        m.tankTurnLeftSpeed = 1.0f;
                    }
                }
                if ((buttons & rightMask) != 0) {
                    if (!inSightAim_) {
                        m.digitalStepRight = true;
                    } else {
                        m.tankTurnRightSpeed = 1.0f;
                    }
                }

                m.digitalStepForward = !inSightAim_ && (buttons & upMask) != 0;
                m.digitalStepBack = !inSightAim_ && (buttons & downMask) != 0;
                m.canNaturalPitch = !inSightAim_;
                m.canNaturalTurn = !inSightAim_;
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

            // TODO(https://github.com/liz-baker/frigate/issues/153): the
            // remote mine's detonation chord.
            // TODO(https://github.com/liz-baker/frigate/issues/148): the tank
            // turret controls of aim mode.
        }

        m.triggerOn = ((buttons & shootButtons) != 0) && ((buttons & invButtons) == 0);

        // 1.2 and 1.4
        if (control == ControlConfig::Solitare || control == ControlConfig::Goodnight) {
            m.disableLookAhead = true;
        }
    }

    usePressed_ = false;
    if (m.btap) {
        // TODO(https://github.com/liz-baker/frigate/issues/148): B enters or
        // leaves the tank.
        usePressed_ = true;
    }

    if (!m.invertPitch) {
        m.controlStickYRaw = -stickY;
        m.analogPitch = -m.analogPitch;
        const float down = m.speedVertaDown;
        m.speedVertaDown = m.speedVertaUp;
        m.speedVertaUp = down;
    }

    // TODO(https://github.com/liz-baker/frigate/issues/146): the damage
    // display suppresses the trigger.
    // TODO(https://github.com/liz-baker/frigate/issues/153): gunTickGameplay
    // with `triggerOn`, the sight visibility, the sniper zoom (`zoomInFovPersec`,
    // `zoomOutFovPersec`, `zooming`), and the noise the guards hear
    // (chrCheckGuardsHeardSound, #109).
    // TODO(https://github.com/liz-baker/frigate/issues/147): the watch's zoom.

    // On foot.
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
    // TODO(https://github.com/liz-baker/frigate/issues/147): these two blocks
    // run only while the watch is closed.
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
            // chrlvStanPointPointIntersection: where the look direction meets
            // the edge that blocked the line.
            Vec3 edgeA{};
            Vec3 edgeB{};
            stan.CollisionEdge(&edgeA, &edgeB);
            const float along =
                ((thetaTransform_[0] * (edgeA[2] - collisionPosition_[2])) -
                 (thetaTransform_[2] * (edgeA[0] - collisionPosition_[0]))) /
                ((thetaTransform_[2] * (edgeB[0] - edgeA[0])) - (thetaTransform_[0] * (edgeB[2] - edgeA[2])));
            ahead = { edgeA[0] + ((edgeB[0] - edgeA[0]) * along), edgeA[1] + ((edgeB[1] - edgeA[1]) * along),
                      edgeA[2] + ((edgeB[2] - edgeA[2]) * along) };

            const float dx = ahead[0] - collisionPosition_[0];
            const float dz = ahead[2] - collisionPosition_[2];
            lookDistance = std::sqrt((dx * dx) + (dz * dz));
            ahead[1] = FloorHeight(stan, aheadTile, ahead[0], ahead[2]);
        }

        if (lookDistance > 0) {
            const float rise = ahead[1] - stanHeight_;
            if (rise > -300.0f && rise < 500.0f) {
                targetPitch = ((std::atan2(rise, lookDistance) * 360.0f) / kTau) + -4.0f;
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

    // Manual pitch input stops the automatic look ahead.
    if (m.speedVertaDown > 0 || m.speedVertaUp > 0) {
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

    if (doCentreUpDown_) {
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
    } else {
        if (m.canNaturalPitch) {
            const float fovScale = context.fovY / 60.0f;
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
    }

    if (m.canNaturalTurn) {
        float turn = static_cast<float>(m.analogTurn) / kStickRange;
        if (turn > 1) {
            turn = 1;
        } else if (turn < -1) {
            turn = -1;
        }
        turn = turn >= 0 ? turn * turn : turn * -turn;
        speedTheta_ = turn * (context.fovY / kFovY);
    } else if (m.aimTurnLeftSpeed > 0) {
        UpdateSpeedTheta(m.aimTurnLeftSpeed);
    } else if (m.aimTurnRightSpeed > 0) {
        UpdateSpeedTheta(-m.aimTurnRightSpeed);
    } else {
        UpdateSpeedTheta(0);
    }

    // TODO(https://github.com/liz-baker/frigate/issues/153): the remote mine
    // detonation and weapon cycling (`weaponBackOffset`, `weaponForwardOffset`).
    if (m.canSwivelGun) {
        controlDef_ = ControlConfig::Honey;
    } else if (m.canManualAim) {
        controlDef_ = ControlConfig::Kissy;
    }

    if (context.weapons != nullptr) {
        if (controlDef_ == ControlConfig::Honey) {
            context.weapons->SetAimType(0);
            // TODO(https://github.com/liz-baker/frigate/issues/166): the auto aim
            // (autoaimx and autoaimy) that chrpropUpdateAutoaimTarget picks
            // while `m.canAutoAim`, in place of the turn speeds.
            context.weapons->AimSwivel(speedTheta_ * 0.3f, -speedVerta_ * 0.1f, context.clockTimer, context.gunView);
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
    bool moved = false;
    int probeTile = tile_;
    const int cdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;

    float radius;
    float height;
    float always30;
    CollisionRadius(&radius, &height, &always30);

    // The player's own prop is not in the way of its own move
    // (sub_GAME_7F03D058).
    // TODO(https://github.com/liz-baker/frigate/issues/148): a world tank's prop
    // is hidden too.
    propCollides_ = false;
    const StanTileProbe probe = stan.TileDistance(probeTile, position[0], position[2], radius);
    if (probe.forceCrouch) {
        autoCrouchPosition_ = kCrouchSquat;
    }

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
            }
        }
    }
    propCollides_ = true;
    // TODO(https://github.com/liz-baker/frigate/issues/148): a blocked move
    // against a tank prop makes it the world tank.
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
    // TODO(https://github.com/liz-baker/frigate/issues/148): the world tank's
    // bounds decide whether the player is standing on it, and set its height
    // offset.

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

    // bondviewUpdatePlayerRoom.
    if (props_ != nullptr) {
        props_->DeregisterRoom(prop_, room_);
    }
    if (tile_ != kNoTile) {
        room_ = stan.TileRoom(tile_);
        if (props_ != nullptr) {
            props_->RegisterRoom(prop_, room_);
        }
        // TODO(https://github.com/liz-baker/frigate/issues/136):
        // objectivestatusCheckRoomEntered.
    }
}

void PlayerMove::UpdatePlayerY(Stan& stan, bool useStanHeight, float stanHeightOffset) {
    const float delta = context_.globalTimerDelta;
    const float x = collisionPosition_[0];
    const float z = collisionPosition_[2];

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
        if (static_cast<int32_t>(stan.TestLocusEdgeAboveY(tile_, x, z, radius, DuckingHeightRelated() + floor)) >= 0) {
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

        // TODO(https://github.com/liz-baker/frigate/issues/136): the fast-bond
        // debug flag's faster fall.
        const float gravity = 0.27777779f;

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
    if (!propCollides_) {
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

    // TODO(https://github.com/liz-baker/frigate/issues/146): the camera stays
    // on the body while a death camera shows it.
    position_ = collisionPosition_;

    // TODO(https://github.com/liz-baker/frigate/issues/146): field_29C0 decays
    // while the player is dead.
    if (verta_ < 0.0f) {
        position_[1] += -(1.0f - cosVerta_) * lookDownDrop_;
    }

    int portalTile = tile_;
    stan.WalkTiles(&portalTile, collisionPosition_[0], collisionPosition_[2], position_[0], position_[2]);
    portalTile_ = portalTile;

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

void PlayerMove::Tick(const MoveContext& context, const MoveInput& input, Stan& stan) {
    context_ = context;
    const float delta = context.globalTimerDelta;

    // MoveBond toggles watch_animation_state on a Start edge before anything
    // else, then skips the entire rest of the function (movement, turning,
    // collision) while it's non-zero.
    if ((input.buttons & ~buttonsPressed_) & Buttons::Start) {
        watchOpen_ = !watchOpen_;
    }
    if (watchOpen_) {
        // TODO(https://github.com/liz-baker/frigate/issues/147): the watch
        // menu itself (covert modem select and other item use), its
        // open/close animation (bondviewWatchAnimationTick), and the
        // locked-controls audio stop.
        buttonsPressed_ = input.buttons;
        return;
    }

    Vec3 moveOffset = { 0.0f, 0.0f, 0.0f };
    bool useStanHeight = false;
    float maxSpeed = 0.0f;
    float stanHeightOffset = 0.0f;

    // TODO(https://github.com/liz-baker/frigate/issues/153): g_bondviewForceDisarm,
    // which unequips both hands a few ticks after the intro forces a disarm.
    // currentPlayerSetCameraMode(0) only resets the field the intro camera
    // (#140) reads.
    // TODO(https://github.com/liz-baker/frigate/issues/146):
    // bondviewPlayerTickDamageAndHealth and bondviewPlayerTickExplode.
    ProcessInput(context, input, stan);

    // Crouching halves the speed, and a shot pushes the player.
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

    // Turn.
    float turned = theta_ + (speedTheta_ * delta * 3.5f);
    while (turned < 0.0f) {
        turned += 360.0f;
    }
    while (turned >= 360.0f) {
        turned -= 360.0f;
    }
    theta_ = turned;

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

    // On foot. TODO(https://github.com/liz-baker/frigate/issues/148): the
    // tank's own movement, and the tank engine sounds that fade here (#41).
    {
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

        // TODO(https://github.com/liz-baker/frigate/issues/136): the fast-bond
        // debug flag's tenfold offset.
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
    // TODO(https://github.com/liz-baker/frigate/issues/148): the tank's props
    // collision pass.

    UpdatePlayerY(stan, useStanHeight, stanHeightOffset);
    UpdateCollisionPositionFields(stan);

    // bondviewUpdatePlayerCollisionBounds.
    collisionBounds_ = { collisionPosition_[0] + collisionRadius_,
                         collisionPosition_[2],
                         collisionPosition_[0],
                         collisionPosition_[2] + collisionRadius_,
                         collisionPosition_[0] - collisionRadius_,
                         collisionPosition_[2],
                         collisionPosition_[0],
                         collisionPosition_[2] - collisionRadius_ };

    buttonsPressed_ = input.buttons;
}

} // namespace Frigate::Level
