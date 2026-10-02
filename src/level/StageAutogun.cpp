#include <cmath>

#include "level/ApplySpeed.h"
#include "level/Autogun.h"
#include "level/ChrActions.h"
#include "level/Stage.h"
#include "level/WeaponTable.h"
#include "model/GameMath.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;

// 2.4 degrees a second, and the same again each second.
constexpr float kYawMaxSpeed = 0.00069813174f;
constexpr float kYawAccel = 0.000011635529f;
// 1.2 degrees a second, and the same again each second.
constexpr float kPitchAccel = 0.0000058177643f;
constexpr float kPitchMaxSpeed = 0.00034906587f;
// Turning at the player.
constexpr float kAlertAccel = 0.0008726647f;
// Two seconds.
constexpr int32_t kTrackingTicks = 120;
// The barrels' spin, 34.3 degrees a second a second up to 5.7 turns a second.
constexpr float kSpinAccel = 0.009973311f;
constexpr float kSpinMaxSpeed = 0.5983986f;

// DegToRad, with the product taken in single precision as the game's macro does.
constexpr float DegToRad(int degrees) {
    return static_cast<float>(static_cast<double>(static_cast<float>(degrees) * kTau) / 360.0);
}

// U32_TO_F32(randomGetNext()).
float RandomUnit() {
    return static_cast<float>(RandomGetNext()) * (1.0f / 4294967296.0f);
}

} // namespace

void Stage::TickAutogun(PlacedObject& object) {
    if (!object.autogun || (object.flags & ObjectFlag::DroneGun) != 0 || player_.PropId() < 0 || object.prop < 0) {
        return;
    }
    AutogunState& gun = *object.autogun;
    const Prop& playerProp = objects_.Props()[player_.PropId()];
    const Prop& mine = objects_.Props()[object.prop];
    const int32_t clockTimer = clock_.clockTimer;
    const float delta = clock_.globalTimerDelta;

    bool sees = false;
    bool tracking = false;
    bool hasLineOfSight = false;

    // PROPFLAG_IS_DOUBLE, read from flags2: a gun that idles about instead of
    // watching for the player.
    if ((object.flags2 & ObjectFlag2::DoorAltCoordSystem) != 0) {
        if ((object.flags2 & ObjectFlag2::DoorCloseFirst) != 0) {
            gun.restPitch = gun.pitch;
            gun.restYaw = gun.yaw;
        } else if (gun.yaw == gun.restYaw && gun.pitch == gun.restPitch) {
            gun.restPitch = (((RandomUnit() * 39.0f) + 1.0f) * kTau) / 360.0f;
            gun.restYaw = RandomUnit() * kTau;
        }

        ChrObjCallsApplySpeed(&gun.yaw, gun.restYaw, &gun.yawSpeed, kYawAccel, kYawAccel, kYawMaxSpeed, clockTimer);
        ChrObjCallsApplySpeed(&gun.pitch, gun.restPitch, &gun.pitchSpeed, kPitchAccel, kPitchAccel, kPitchMaxSpeed,
                              clockTimer);
        return;
    }

    float toPlayerX = playerProp.position[0] - object.runtimePosition[0];
    // It aims 20 units below the player's head.
    const float toPlayerY = (playerProp.position[1] - object.runtimePosition[1]) - 20.0f;
    const float toPlayerZ = playerProp.position[2] - object.runtimePosition[2];
    float horizontalDistSq = (toPlayerX * toPlayerX) + (toPlayerZ * toPlayerZ);
    const float horizontalDist = std::sqrt(horizontalDistSq);
    float distanceToPlayer = horizontalDist;
    if ((object.flags & ObjectFlag::DoorTwoWay) != 0) {
        horizontalDistSq += toPlayerY * toPlayerY;
        distanceToPlayer = std::sqrt(horizontalDistSq);
    }

    float aimLimit = ChrActions::AimLimitAngle(horizontalDistSq);
    float wantedYaw = gun.restYaw;
    float wantedPitch = gun.restPitch;
    if (distanceToPlayer <= gun.aimDistance) {
        const float playerYaw = GameAtan2f(toPlayerX, toPlayerZ);
        const float playerPitch = GameAtan2f(toPlayerY, horizontalDist);
        if ((object.flags & ObjectFlag::WeaponNoAmmo) != 0 || (object.flags & ObjectFlag::InMotion) != 0) {
            sees = true;
        } else {
            float yawError = playerYaw - gun.yaw;
            if (yawError < 0.0f) {
                yawError += kTau;
            }
            if (yawError > kPi) {
                yawError -= kTau;
            }
            if (yawError < DegToRad(70) && yawError > DegToRad(-70)) {
                sees = true;
            }
        }

        if (sees) {
            float restError = playerYaw - gun.restYaw;
            int tile = mine.stan;
            if (restError < -kPi) {
                restError += kTau;
            } else if (restError >= kPi) {
                restError -= kTau;
            }

            player_.SetCollides(false);
            if (restError <= gun.limitMax && gun.limitMin <= restError &&
                level_.StanFile().TestLineUnobstructed(&tile, mine.position[0], mine.position[2],
                                                       playerProp.position[0], playerProp.position[2], 0x1B,
                                                       mine.position[1], mine.position[1], playerProp.position[1],
                                                       playerProp.position[1]) != 0 &&
                tile == playerProp.stan) {
                object.flags |= ObjectFlag::InMotion;
                hasLineOfSight = true;
                wantedYaw = playerYaw;
                wantedPitch = playerPitch;
            } else if (gun.lastSighted >= 0 && (clock_.globalTimer - kTrackingTicks) < gun.lastSighted) {
                wantedYaw = gun.yaw;
                wantedPitch = gun.pitch;
            } else {
                sees = false;
            }
            player_.SetCollides(true);
        }
    }

    if (sees) {
        aimLimit = ChrActions::AimLimitAngle(horizontalDistSq);
    }

    if (gun.active) {
        // It sways about the player once every two seconds while it fires.
        wantedYaw += (aimLimit * 0.8f) *
                     GameSinf((static_cast<float>(clock_.globalTimer % kTrackingTicks) * kTau) /
                              static_cast<float>(kTrackingTicks));
        if (wantedYaw < 0.0f) {
            wantedYaw += kTau;
        }
        if (wantedYaw >= kTau) {
            wantedYaw -= kTau;
        }
    }

    float fromRest = wantedYaw - gun.restYaw;
    if (fromRest < -kPi) {
        fromRest += kTau;
    } else if (fromRest >= kPi) {
        fromRest -= kTau;
    }
    if (gun.limitMax < fromRest) {
        wantedYaw = gun.restYaw + gun.limitMax;
    } else if (fromRest < gun.limitMin) {
        wantedYaw = gun.restYaw + gun.limitMin;
    }
    if (wantedYaw < 0.0f) {
        wantedYaw += kTau;
    }
    if (wantedYaw >= kTau) {
        wantedYaw -= kTau;
    }

    ChrObjCallsApplySpeed(&gun.yaw, wantedYaw, &gun.yawSpeed, kAlertAccel, kAlertAccel, gun.speed, clockTimer);
    ChrObjCallsApplySpeed(&gun.pitch, wantedPitch, &gun.pitchSpeed, kAlertAccel, kAlertAccel, gun.speed, clockTimer);

    float yawLeft = wantedYaw - gun.yaw;
    if (yawLeft < 0.0f) {
        yawLeft += kTau;
    }
    if (yawLeft > kPi) {
        yawLeft -= kTau;
    }
    float pitchLeft = wantedPitch - gun.pitch;
    if (pitchLeft < 0.0f) {
        pitchLeft += kTau;
    }
    if (pitchLeft > kPi) {
        pitchLeft -= kTau;
    }

    gun.active = false;
    if (sees) {
        if (yawLeft < aimLimit && (-aimLimit) < yawLeft && pitchLeft < aimLimit && (-aimLimit) < pitchLeft) {
            gun.active = true;
            tracking = true;
            if (hasLineOfSight) {
                gun.lastSighted = clock_.globalTimer;
                gun.firstSighted = clock_.globalTimer;
            }
        } else {
            const float looseLimit = 2.0f * aimLimit;
            if (yawLeft < looseLimit && (-looseLimit) < yawLeft && pitchLeft < looseLimit &&
                (-looseLimit) < pitchLeft) {
                gun.active = true;
                tracking = true;
                if (hasLineOfSight) {
                    gun.lastSighted = clock_.globalTimer;
                }
            } else if (gun.lastSighted >= 0 && (clock_.globalTimer - kTrackingTicks) < gun.lastSighted) {
                gun.active = true;
                tracking = true;
            }
        }
    }

    if (tracking) {
        gun.spinSpeed += kSpinAccel * delta;
        if (gun.spinSpeed > kSpinMaxSpeed) {
            gun.spinSpeed = kSpinMaxSpeed;
        }
    } else if (gun.spinSpeed > 0.0f) {
        // The barrels lose 45% of their spin a second when idle.
        for (int32_t tick = 0; tick < clockTimer; tick++) {
            gun.spinSpeed *= 0.99f;
        }
        if (gun.spinSpeed <= 0.0001f) {
            gun.spinSpeed = 0.0f;
        }
    }

    if (gun.spinSpeed > 0.0f) {
        gun.spinAngle += gun.spinSpeed * delta;
        while (gun.spinAngle >= kTau) {
            gun.spinAngle -= kTau;
        }
    }
}

void Stage::FireAutogun(PlacedObject& object) {
    if (!object.autogun || object.prop < 0 || ControlsLocked() || player_.PropId() < 0) {
        return;
    }
    AutogunState& gun = *object.autogun;
    Prop& mine = objects_.Props()[object.prop];
    const Prop& playerProp = objects_.Props()[player_.PropId()];
    const SwitchGeometry* geometry = objects_.SwitchGeometryOf(object);
    bool flashA = false;
    bool flashB = false;

    if (gun.active && (object.flags & ObjectFlag::DroneGun) == 0) {
        gun.shots++;
        flashA = (gun.shots & 1) == 0;
        const bool hasSecondFlash = geometry != nullptr && geometry->matrix[7] >= 0;
        if (geometry != nullptr && geometry->matrix[5] >= 0) {
            flashB = (gun.shots & 1) == 1;
        }

        if (gun.soundTick < clock_.globalTimer) {
            // TODO(https://github.com/liz-baker/frigate/issues/41): the cannon
            // sound it starts here (GUN_B9_CANNON_SHORT_SFX).
            gun.soundTick = clock_.globalTimer + 2;
        }

        if (flashA || flashB) {
            bool hitsPlayer = true;
            int hitTile = kNoTile;
            int tile = mine.stan;
            const bool beamThisShot = (gun.shots & 3) == 0;

            int slot = 5;
            if (hasSecondFlash && (gun.shots & 7) == 0) {
                slot = 7;
            }

            Vec3 muzzle = mine.position;
            if ((mine.flags & PropFlag::OnScreen) != 0 && geometry != nullptr && geometry->matrix[slot] >= 0) {
                Mtxf body = object.mtx;
                for (size_t axis = 0; axis < 3; axis++) {
                    body.m[3][axis] = object.runtimePosition[axis];
                }
                const AutogunPose pose = PoseAutogun(*geometry, gun, body, object.scale);
                const Mtxf node = AutogunNodeMatrix(*geometry, pose, body, static_cast<size_t>(slot));
                const Vec3 offset = { geometry->origin[static_cast<size_t>(slot)][0],
                                      geometry->origin[static_cast<size_t>(slot)][1],
                                      geometry->origin[static_cast<size_t>(slot)][2] };
                muzzle = TransformVector(node, offset);
                if (!level_.StanFile().WalkTiles(&tile, mine.position[0], mine.position[2], muzzle[0], muzzle[2])) {
                    muzzle = mine.position;
                }
            }

            const Vec3 direction = { GameCosf(gun.pitch) * GameSinf(gun.yaw), GameSinf(gun.pitch),
                                     GameCosf(gun.pitch) * GameCosf(gun.yaw) };
            Vec3 end = { muzzle[0] + (direction[0] * 65536.0f), muzzle[1] + (direction[1] * 65536.0f),
                         muzzle[2] + (direction[2] * 65536.0f) };

            level_.StanFile().ResetHits();
            if (level_.StanFile().TestLineUnobstructed(&tile, muzzle[0], muzzle[2], end[0], end[2], 2, 100.0f, 100.0f,
                                                       0.0f, 1.0f) == 0) {
                end = aiActions_->EdgeIntersection(muzzle, direction);
                hitTile = tile;
                end[0] -= 26.0f * direction[0];
                end[1] -= 26.0f * direction[1];
                end[2] -= 26.0f * direction[2];
            }

            if (clock_.globalTimer == gun.firstSighted) {
                float dx = playerProp.position[0] - muzzle[0];
                float dy = playerProp.position[1] - muzzle[1];
                float dz = playerProp.position[2] - muzzle[2];
                const float playerDistSq = ((dx * dx) + (dy * dy)) + (dz * dz);
                dx = end[0] - muzzle[0];
                dy = end[1] - muzzle[1];
                dz = end[2] - muzzle[2];
                if (playerDistSq <= (((dx * dx) + (dy * dy)) + (dz * dz)) && !health_.DamageShowing()) {
                    const float distance = std::sqrt(playerDistSq);
                    float built = (0.16f * clock_.globalTimerDelta) * modifiers_.autogunPendingDamageTick;
                    if (distance > 200.0f) {
                        built *= 200.0f / distance;
                    }
                    gun.damageBuilt += built;
                    if (gun.damageBuilt >= 1.0f) {
                        const float destruction =
                            weapons_.Ready() ? weapons_.Table().Item(kItemRcp90).destructionAmount : 0.0f;
                        RecordPlayerDamage((destruction * 0.125f) * modifiers_.autogunDamageScalar, GameSinf(gun.yaw),
                                           GameCosf(gun.yaw), true);
                        gun.damageBuilt = 0.0f;
                        if (health_.DamageShowing()) {
                            hitsPlayer = false;
                        }
                    }
                }
            }

            // recall_joy2_hits_edit_flag and recall_joy2_hits_edit_detail_edit_flag
            // pick their ricochet sounds from two draws.
            // TODO(https://github.com/liz-baker/frigate/issues/41): the sounds themselves.
            if (hitsPlayer) {
                if (hitTile != kNoTile) {
                    sparks_.Create(end, 1, 26.0f, level_.StanFile().TileRoom(hitTile), RandomGetNext);
                }
                RandomGetNext();
                RandomGetNext();
            } else {
                end = playerProp.position;
                RandomGetNext();
                RandomGetNext();
            }

            if (beamThisShot) {
                BeamRecord& beam = gun.beam;
                beam.position = muzzle;
                beam.delta = { end[0] - muzzle[0], end[1] - muzzle[1], end[2] - muzzle[2] };
                float length = std::sqrt(((beam.delta[0] * beam.delta[0]) + (beam.delta[1] * beam.delta[1])) +
                                         (beam.delta[2] * beam.delta[2]));
                const float inverse = 1.0f / length;
                beam.delta[0] = beam.delta[0] * inverse;
                beam.delta[1] = beam.delta[1] * inverse;
                beam.delta[2] = beam.delta[2] * inverse;
                if (length > 10000.0f) {
                    length = 10000.0f;
                }
                beam.state = 0;
                beam.item = static_cast<int8_t>(kItemRcp90);
                beam.length = length;
                if (length < 500.0f) {
                    length = 500.0f;
                }
                beam.streak = 0.2f * length;
                beam.drawLength = 0.2f * length;
                if (beam.drawLength > 3000.0f) {
                    beam.drawLength = 3000.0f;
                }
                beam.tail = ((2.0f * RandomUnit()) - 1.0f) * beam.streak;
            }
        }
    }

    gun.flash = { flashA, flashB };
}

} // namespace Frigate::Level
