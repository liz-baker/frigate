#include "level/PlayerMove.h"

#include <cmath>

#include "game/Sound.h"
#include "level/Objects.h"
#include "level/PropList.h"
#include "level/StanIntersection.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;
constexpr float kDegToRad = 0.017453292f;
// The tank collides with these.
constexpr int kTankCdTypes = CdType::Objs | CdType::Doors | CdType::PathBlocker | CdType::ObjsImmuneToExplosions;
// The tank's gun is held within these pitches (about -5 and 25 degrees).
constexpr float kGunPitchMin = -0.087266468f;
constexpr float kGunPitchMax = 25.0f * kDegToRad;
// A shell fired from the sight points a little above the sight (10 degrees).
constexpr float kGunPitchLead = 0.17453294f;

float WrapRadians(float angle) {
    if (angle >= kTau) {
        angle -= kTau;
    }
    if (angle < 0.0f) {
        angle += kTau;
    }
    return angle;
}

} // namespace

Vec3 PlayerMove::TankAimDirection() const {
    const float heading = WrapRadians(tankOrientation_ + tankTurretRad_);
    return { -GameSinf(heading) * GameCosf(tankTurretVertical_), GameSinf(tankTurretVertical_),
             GameCosf(heading) * GameCosf(tankTurretVertical_) };
}

void PlayerMove::SetCollides(bool collides) {
    if (body_.setMoving) {
        body_.setMoving(collides);
    }
    if (playerTank_ >= 0 && context_.tank != nullptr) {
        context_.tank->SetCollides(playerTank_, collides);
    }
    propCollides_ = collides;
}

float PlayerMove::TankFloor(const Stan& stan) const {
    const Prop& prop = (*props_)[playerTank_];
    return stan.PositionY(prop.stan, prop.position[0], prop.position[2]) + tankYOffset_;
}

void PlayerMove::UpdateTankModelOffset() {
    TankHooks* hooks = context_.tank;
    if (playerTank_ < 0 || hooks == nullptr) {
        return;
    }
    const PlacedObject* tank = hooks->TankOf(playerTank_);
    const TankGeometry* geometry = tank != nullptr ? hooks->Geometry(*tank) : nullptr;
    if (geometry != nullptr) {
        tankModelOffset_ = TankModelOffset(*geometry, tankTurretRad_);
    }
}

std::array<Vec2, 8> PlayerMove::TankBounds(const Vec3& position, float orientation) const {
    TankHooks* hooks = context_.tank;
    const PlacedObject* tank = hooks != nullptr && playerTank_ >= 0 ? hooks->TankOf(playerTank_) : nullptr;
    const ModelBounds* hull = tank != nullptr ? hooks->Hull(*tank) : nullptr;
    if (hull == nullptr) {
        return {};
    }
    return TankCollisionBounds(*hull, tankModelOffset_, tank->scale, position, orientation);
}

bool PlayerMove::TankFits(Stan& stan, const Vec3& position, int tile, float orientation, Vec3* lineA, Vec3* lineB) {
    TankHooks* hooks = context_.tank;
    const std::array<Vec2, 8> bounds = TankBounds(position, orientation);

    if (playerTank_ >= 0 && hooks != nullptr) {
        hooks->SetCollides(playerTank_, false);
    }

    // bondviewTestLineUnobstructed: the segment that was blocked is handed back.
    int current = tile;
    const auto line = [&](float fromX, float fromZ, float toX, float toZ) {
        const int result =
            stan.TestLineUnobstructed(&current, fromX, fromZ, toX, toZ, kTankCdTypes, 0.0f, 1.0f, 0.0f, 1.0f);
        if (result == 0 && lineA != nullptr) {
            *lineA = { fromX, 0.0f, fromZ };
            *lineB = { toX, 0.0f, toZ };
        }
        return result != 0;
    };

    bool fits = false;
    if (line(position[0], position[2], bounds[0][0], bounds[0][1]) &&
        line(bounds[0][0], bounds[0][1], bounds[1][0], bounds[1][1]) &&
        line(bounds[1][0], bounds[1][1], bounds[2][0], bounds[2][1]) &&
        line(bounds[2][0], bounds[2][1], bounds[3][0], bounds[3][1]) &&
        line(bounds[3][0], bounds[3][1], bounds[0][0], bounds[0][1])) {
        fits = true;

        const PlacedObject* tank = playerTank_ >= 0 && hooks != nullptr ? hooks->TankOf(playerTank_) : nullptr;
        const TankGeometry* geometry = tank != nullptr ? hooks->Geometry(*tank) : nullptr;
        if (geometry != nullptr) {
            // From the seat to the gun's muzzle, turned by where the turret faces.
            Vec3 muzzle = { geometry->nodes[3][0] + geometry->nodes[2][0] - geometry->nodes[1][0], 0.0f,
                            geometry->nodes[3][2] + geometry->nodes[2][2] - geometry->nodes[1][2] };
            const float heading = WrapRadians(orientation + tankTurretRad_);
            muzzle = RotateVector(RotationY(kTau - heading), muzzle);
            muzzle[0] *= tank->scale;
            muzzle[2] *= tank->scale;
            muzzle[0] += position[0];
            muzzle[2] += position[2];

            current = tile;
            if (!line(position[0], position[2], muzzle[0], muzzle[2])) {
                fits = false;
            }
        }
    }

    if (playerTank_ >= 0 && hooks != nullptr) {
        hooks->SetCollides(playerTank_, true);
    }
    return fits;
}

bool PlayerMove::TankTryMove(Stan& stan, const Vec3& position, int* tile) {
    int current = tile_;
    if (stan.TestLineUnobstructed(&current, collisionPosition_[0], collisionPosition_[2], position[0], position[2], 0,
                                  0.0f, 1.0f, 0.0f, 1.0f) != 0 &&
        TankFits(stan, position, current, tankOrientation_, nullptr, nullptr)) {
        *tile = current;
        return true;
    }
    return false;
}

void PlayerMove::UseTank() {
    TankHooks* hooks = context_.tank;
    PlacedObject* tank = hooks->TankOf(playerTank_);
    if (inTank_) {
        tank->tank.ammo = hooks->LeaveTank();
        tank->tank.firing = false;
        inTank_ = false;
        speedSideways_ = 0.0f;
        speedForwards_ = 0.0f;
        speedTheta_ = 0.0f;
        shotSpeed_ = { 0.0f, 0.0f, 0.0f };
        crouchPosition_ = kCrouchStand;
        return;
    }

    hooks->EnterTank(tank->tank.ammo);
    tank->tank.ammo = 0;
    tankTurretVertical_ = tank->tank.turretVerticalAngle;
    tankTurretVerticalRelated_ = tankTurretVertical_ / kTankVertAngleFactor;
    tankTurretAngle_ = tank->tank.turretOrientationAngle;
    tankTurretRad_ = tank->tank.turretOrientationAngle;
    tankTurretDeg_ = tankTurretRad_ / kTankVertAngleRadFactor;
    tankTurretTurnSpeed_ = 0.0f;
    tankOrientation_ = tank->tank.tankOrientationAngle;
    tankTurnSpeed_ = 0.0f;
    inTank_ = true;
    tankRunState_ = kTankNotRunning;
    speedSideways_ = 0.0f;
    speedForwards_ = 0.0f;
    speedTheta_ = 0.0f;
    crouchPosition_ = kCrouchHalf;
    tankSitHeight_ = 0.0f;
    tankSitRemain_ = 1.0f;
    tankEnterTheta_ = theta_;
    tankEnterVerta_ = verta_;
    tankEnterCoord_ = collisionPosition_;
    tankDamagePenalty_ = 0;

    UpdateTankModelOffset();
}

bool PlayerMove::UpdateWorldTank(Stan& stan) {
    (void)stan;
    TankHooks* hooks = context_.tank;
    PropCollider bounds;
    const bool hasBounds = hooks->CollisionBounds(worldTank_, &bounds);
    const int edges = hasBounds ? bounds.edges : 0;

    if (inTank_ || PointInPolygon(collisionPosition_, bounds.polygon, edges) ||
        PointPolygonCollision(collisionPosition_, collisionRadius_, bounds.polygon, edges)) {
        PlacedObject* tank = hooks->TankOf(worldTank_);
        const TankGeometry* geometry = tank != nullptr ? hooks->Geometry(*tank) : nullptr;
        if (tank == nullptr || geometry == nullptr) {
            return false;
        }
        playerTank_ = worldTank_;

        float height = (geometry->hull[3] - geometry->hull[2]) * tank->scale;
        if (inTank_ || PointInPolygon(collisionPosition_, tank->tank.rect, tank->tank.rectEdges)) {
            height += (geometry->top[3] - geometry->top[2]) * tank->scale;
            canEnterTank_ = true;
        }

        if (!inTank_ && tankYOffset_ < height) {
            // The player climbs onto the tank, and does not move meanwhile.
            tankYOffset_ += 20.0f * context_.globalTimerDelta;
            if (!(height < tankYOffset_)) {
                return true;
            }
            tankYOffset_ = height;
        } else {
            tankYOffset_ = height;
        }

        if (inTank_) {
            if (tankRunState_ == kTankNotRunning) {
                tankYOffset_ += -37.0f * (1.0f - tankSitRemain_);
            } else {
                tankYOffset_ += -37.0f;
            }
        }
    } else if (playerTank_ >= 0) {
        worldTank_ = -1;
        playerTank_ = -1;
        tankYOffset_ = 0.0f;
    }
    return false;
}

void PlayerMove::TurnTank(Stan& stan) {
    const float delta = context_.globalTimerDelta;
    float tankChange = ((speedTheta_ * delta) * kDegToRad) * 3.5f;
    const float turned = WrapRadians(tankOrientation_ + tankChange);

    Vec3 lineA{};
    Vec3 lineB{};
    if (TankFits(stan, collisionPosition_, tile_, turned, &lineA, &lineB)) {
        tankOrientation_ = turned;
    } else {
        // The turn is blocked: back the player off the blocking edge and try
        // again.
        float firstDepth = -1.0f;
        float secondDepth = -1.0f;

        Vec3 edgeA{};
        Vec3 edgeB{};
        stan.CollisionEdge(&edgeA, &edgeB);

        float dx = edgeB[0] - edgeA[0];
        float dz = edgeB[2] - edgeA[2];
        float norm = 1.0f / std::sqrt((dx * dx) + (dz * dz));
        dx *= norm;
        dz *= norm;
        float firstX = dz;
        float firstZ = -dx;

        float depth = ((collisionPosition_[0] - lineB[0]) * firstX) + ((collisionPosition_[2] - lineB[2]) * firstZ);
        if (depth < 0.0f) {
            depth = -depth;
            firstX = -firstX;
            firstZ = -firstZ;
        }
        float reach = ((collisionPosition_[0] - edgeA[0]) * firstX) + ((collisionPosition_[2] - edgeA[2]) * firstZ);
        if (reach < depth) {
            firstDepth = depth - reach;
        }

        dx = lineB[0] - lineA[0];
        dz = lineB[2] - lineA[2];
        norm = 1.0f / std::sqrt((dx * dx) + (dz * dz));
        dx *= norm;
        dz *= norm;
        float secondX = dz;
        float secondZ = -dx;

        depth = ((collisionPosition_[0] - lineB[0]) * secondX) + ((collisionPosition_[2] - lineB[2]) * secondZ);
        if (depth < 0.0f) {
            depth = -depth;
            secondX = -secondX;
            secondZ = -secondZ;
        }
        reach = ((collisionPosition_[0] - edgeA[0]) * secondX) + ((collisionPosition_[2] - edgeA[2]) * secondZ);
        const float reachB =
            ((collisionPosition_[0] - edgeB[0]) * secondX) + ((collisionPosition_[2] - edgeB[2]) * secondZ);
        if (reachB < reach) {
            reach = reachB;
        }
        if (reach < depth) {
            secondDepth = depth - reach;
        }

        if (secondDepth >= 0.0f && (secondDepth < firstDepth || firstDepth < 0.0f)) {
            firstX = secondX;
            firstZ = secondZ;
            firstDepth = secondDepth;
        }

        if (firstDepth >= 0.0f) {
            const Vec3 push = { firstDepth * firstX * 1.01f, 0.0f, firstDepth * firstZ * 1.01f };
            CalcUpdateCollision(stan, push, true);

            if (TankFits(stan, collisionPosition_, tile_, turned, &lineA, &lineB)) {
                tankOrientation_ = turned;
            } else {
                tankChange = 0.0f;
            }
        } else {
            tankChange = 0.0f;
        }
    }

    const float previousTurret = tankTurretRad_;
    tankTurretAngle_ += tankTurretTurn_;
    tankTurretAngle_ = WrapRadians(tankTurretAngle_);

    float target = (((speedTheta_ * 3.5f) * kDegToRad) * 4.0f) + tankTurretAngle_;
    if (target < 0.0f) {
        target += kTau;
    }
    if (target >= kTau) {
        target -= kTau;
    }
    if ((target - tankTurretRad_) >= kPi) {
        target -= kTau;
    } else if ((target - tankTurretRad_) < -kPi) {
        target += kTau;
    }

    for (int i = 0; i < context_.clockTimer; i++) {
        tankTurretDeg_ = (kTankRotationScale * tankTurretDeg_) + target;
    }
    tankTurretRad_ = tankTurretDeg_ * (1.0f - kTankRotationScale);

    if (tankTurretRad_ >= kTau) {
        tankTurretRad_ -= kTau;
        tankTurretDeg_ = tankTurretRad_ / (1.0f - kTankRotationScale);
    }
    if (tankTurretRad_ < 0.0f) {
        tankTurretRad_ += kTau;
        tankTurretDeg_ = tankTurretRad_ / (1.0f - kTankRotationScale);
    }

    if (!TankFits(stan, collisionPosition_, tile_, tankOrientation_, nullptr, nullptr)) {
        tankTurretRad_ = previousTurret;
        tankTurretDeg_ = tankTurretRad_ / (1.0f - kTankRotationScale);
        tankTurretAngle_ = previousTurret;
    }

    if (playerTank_ >= 0 && context_.tank != nullptr) {
        const PlacedObject* tank = context_.tank->TankOf(playerTank_);
        if (tank != nullptr) {
            Vec3 shift = { tankModelOffset_[0], 0.0f, tankModelOffset_[2] };
            shift = RotateVector(RotationY(tankChange), shift);
            UpdateTankModelOffset();

            shift = { tankModelOffset_[0] - shift[0], 0.0f, tankModelOffset_[2] - shift[2] };
            Mtxf rotation = RotationY(kTau - tankOrientation_);
            ScaleRows(&rotation, tank->scale);
            shift = RotateVector(rotation, shift);
            CalcUpdateCollision(stan, shift, true);
        }
    }

    if (context_.clockTimer > 0) {
        for (int i = 0; i < context_.clockTimer; i++) {
            tankTurretTurnSpeed_ = (kTankRotationScale * tankTurretTurnSpeed_) + (tankTurretTurn_ / delta);
        }
    }

    const float turretSpeed = tankTurretTurnSpeed_ * (1.0f - kTankRotationScale);
    theta_ = (tankOrientation_ + tankTurretRad_ + (((speedTheta_ * 3.5f) * kDegToRad) * 4.0f) + (turretSpeed * 4.0f)) *
             360.0f / kTau;
    while (theta_ < 0.0f) {
        theta_ += 360.0f;
    }
    while (theta_ >= 360.0f) {
        theta_ -= 360.0f;
    }
}

void PlayerMove::DriveTank(Stan& stan, Vec3* moveOffset) {
    TankHooks* hooks = context_.tank;
    const float delta = context_.globalTimerDelta;
    *moveOffset = { 0.0f, 0.0f, 0.0f };

    if (tankRunState_ == kTankNotRunning) {
        // The player settles into the seat, turning to face where the turret does.
        const PlacedObject* tank = playerTank_ >= 0 && hooks != nullptr ? hooks->TankOf(playerTank_) : nullptr;
        if (tank != nullptr) {
            Mtxf rotation = RotationY(kTau - tankOrientation_);
            ScaleRows(&rotation, tank->scale);
            Vec3 seat = RotateVector(rotation, tankModelOffset_);
            for (size_t axis = 0; axis < 3; axis++) {
                seat[axis] += tank->runtimePosition[axis];
            }

            float heading = ((tankOrientation_ + tankTurretRad_) * 360.0f) / kTau;
            float pitch = verta_;
            if (pitch < -20.0f) {
                pitch = -20.0f;
            }

            tankSitHeight_ += delta / 45.0f;
            if (tankSitHeight_ >= 1.0f) {
                tankSitHeight_ = 1.0f;
            }
            tankSitRemain_ = (GameCosf(tankSitHeight_ * kTau * 0.5f) + 1.0f) * 0.5f;

            verta_ = (tankSitRemain_ * tankEnterVerta_) + ((1.0f - tankSitRemain_) * pitch);

            const float turn = heading - tankEnterTheta_;
            if (turn > 180.0f) {
                heading -= 360.0f;
            }
            if (turn < -180.0f) {
                heading += 360.0f;
            }
            theta_ = (tankSitRemain_ * tankEnterTheta_) + ((1.0f - tankSitRemain_) * heading);
            if (theta_ >= 360.0f) {
                theta_ -= 360.0f;
            }
            if (theta_ < 0.0f) {
                theta_ += 360.0f;
            }

            (*moveOffset)[0] =
                ((tankSitRemain_ * tankEnterCoord_[0]) + ((1.0f - tankSitRemain_) * seat[0])) - collisionPosition_[0];
            (*moveOffset)[1] = 0.0f;
            (*moveOffset)[2] =
                ((tankSitRemain_ * tankEnterCoord_[2]) + ((1.0f - tankSitRemain_) * seat[2])) - collisionPosition_[2];
        }

        if (tankSitHeight_ >= 1.0f) {
            tankRunState_ = kTankStarting;
        }
    } else if (tankRunState_ == kTankStarting) {
        tankRunState_ = kTankRunning;
        constexpr int32_t kEngineStartVolume = 0x61A8;
        if (tankSound_[0] == nullptr && !context_.controlsLocked) {
            Game::Sound::PlaySfx(AudioDriver::kSfxTruckStart, &tankSound_[0]);
        }
        Game::Sound::SetVolume(tankSound_[0], kEngineStartVolume);
        tankEngineVolume_ = kEngineStartVolume;
    } else {
        // The engine sounds follow the larger of the forward and turning speeds.
        constexpr float kTankMaxTurnSpeed = 0.3f;
        float utilization = std::fabs(speedForwards_ / kTankMaxSpeed);
        const float turnUtilization = std::fabs(speedTheta_ / kTankMaxTurnSpeed);
        if (utilization < turnUtilization) {
            utilization = turnUtilization;
        }
        if (utilization > 0.0f) {
            if (utilization > 1.0f) {
                utilization = 1.0f;
            }
            if (tankSound_[1] == nullptr && !context_.controlsLocked) {
                Game::Sound::PlaySfx(AudioDriver::kSfxTank, &tankSound_[1]);
            }
            if (tankSound_[1] != nullptr) {
                int32_t volume = 0x7FFF;
                if (utilization < 0.15f) {
                    volume = static_cast<int32_t>((utilization * 20000.0f) / 0.15f);
                } else if (utilization < 0.9f) {
                    volume = static_cast<int32_t>((((utilization - 0.15f) * 12767.0f) / 0.75f) + 20000.0f);
                }
                Game::Sound::SetVolume(tankSound_[1], volume);
            }
        } else if (tankSound_[1] != nullptr && Game::Sound::GetPlayingState(tankSound_[1]) != 0) {
            Game::Sound::Deactivate(tankSound_[1]);
        }
        if (tankSound_[0] == nullptr && !context_.controlsLocked) {
            Game::Sound::PlaySfx(AudioDriver::kSfxTruckRun, &tankSound_[0]);
        }
        if (tankSound_[0] != nullptr) {
            tankEngineVolume_ = 0x7FFF;
            if (utilization < 0.9f) {
                tankEngineVolume_ = static_cast<int32_t>(((utilization * 7767.0f) / 0.9f) + 25000.0f);
            }
            Game::Sound::SetVolume(tankSound_[0], tankEngineVolume_);
        }
        float pitch;
        if (context_.weapons != nullptr && context_.weapons->WeaponId(HandRight) == kItemTankShells) {
            pitch = context_.sightPitch + kGunPitchLead;
        } else {
            pitch = tankTurretVertical_;
        }
        if (pitch > kGunPitchMax) {
            pitch = kGunPitchMax;
        }
        if (pitch < kGunPitchMin) {
            pitch = kGunPitchMin;
        }
        for (int i = 0; i < context_.clockTimer; i++) {
            tankTurretVerticalRelated_ = (kTankTurretVerticalScale * tankTurretVerticalRelated_) + pitch;
        }
        tankTurretVertical_ = tankTurretVerticalRelated_ * (1.0f - kTankTurretVerticalScale);
    }

    breathing_ -= (0.750f * delta) / 2700.0f;
    if (breathing_ < 0.0f) {
        breathing_ = 0.0f;
    }

    // bondviewMoveAnimationTick(0, 0, 0).
    headPosition_ =
        head_ != nullptr ? head_->Tick(0.0f, 0.0f, 0.0f, breathing_, context_) : Vec3{ 0.0f, standHeight_, 0.0f };
    UpdateAppliedView(head_ != nullptr ? head_->Look() : Vec3{ 0.0f, 0.0f, 1.0f },
                      head_ != nullptr ? head_->Up() : Vec3{ 0.0f, 1.0f, 0.0f });

    (*moveOffset)[0] += speedForwards_ * GameSinf(kTau - tankOrientation_) * delta;
    (*moveOffset)[2] += speedForwards_ * GameCosf(kTau - tankOrientation_) * delta;

    CalcUpdateCollision(stan, *moveOffset, true);

    if (tankRunState_ == kTankRunning && context_.clockTimer > 0) {
        const float moveX = (collisionPosition_[0] - previousPosition_[0]) / delta;
        const float moveZ = (collisionPosition_[2] - previousPosition_[2]) / delta;
        float speed = std::sqrt((moveX * moveX) + (moveZ * moveZ));
        if (speedForwards_ < 0.0f) {
            speed = -speed;
        }
        speedForwards_ = speed;
    }
}

void PlayerMove::TickTankProp() {
    TankHooks* hooks = context_.tank;
    PlacedObject* tank = hooks != nullptr ? hooks->TankOf(playerTank_) : nullptr;
    if (tank == nullptr) {
        return;
    }
    TankHooks::Frame frame;
    frame.tank = tank;
    frame.prop = playerTank_;
    frame.collisionPosition = collisionPosition_;
    frame.tile = tile_;
    frame.tankOrientation = tankOrientation_;
    frame.turretVerticalAngle = tankTurretVertical_;
    frame.turretOrientation = tankTurretRad_;
    frame.modelOffset = tankModelOffset_;
    frame.firing = context_.weapons != nullptr && context_.weapons->WeaponId(HandRight) == kItemTankShells &&
                   context_.weapons->Firing(HandRight);
    frame.clockTimer = context_.clockTimer;
    frame.damagePenaltyTicks = tankDamagePenalty_;
    tankDamagePenalty_ = hooks->TickTank(frame);
}

} // namespace Frigate::Level
