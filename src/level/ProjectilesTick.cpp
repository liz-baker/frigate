#include <cmath>
#include <cstring>

#include "level/HitParts.h"
#include "level/LevelModels.h"
#include "level/ModelHit.h"
#include "level/Projectiles.h"
#include "level/WeaponTable.h"
#include "model/Quaternion.h"

// The per-tick half of projectiles: the flight of an object, its fuse, and
// what makes an object fall.
namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kFraction = 2.3283064e-10f;
// ROCKET_INITIAL_GRAVITY_MODIFIER and PROP_PROJECTILE_GRAVITY_MODIFIER.
constexpr float kGravity = 0.27777779f;
// ROCKET_SPEED_BREAK_THRESHOLD and PROJECTILE_FRICTION_FACTOR.
constexpr float kRocketSpeedLimit = 27777.773f;
constexpr float kFriction = 0.9f;

constexpr int kExplosionDrone = 12;
constexpr int kExplosionStandard = 13;
constexpr int kExplosionMassive = 17;
constexpr int kExplosionFacilityRemote = 19;
// PLASTIQUE_EXPLOSION_DELAY_TICKS.
constexpr int kPlastiqueDelay = 120;

bool IsCollectable(const PlacedObject& object) {
    return object.type == static_cast<uint8_t>(PropDefType::Collectable);
}

// The weapon number of a collectable, -1 for any other object.
int32_t WeaponOf(const PlacedObject& object) {
    return IsCollectable(object) ? object.weaponNum : -1;
}

Quaternion FromArray(const std::array<float, 4>& a) {
    return { a[0], a[1], a[2], a[3] };
}

// quaternion_to_matrix into the rotation rows of `matrix`.
void SetRotationFromQuaternion(const Quaternion& q, Mtxf* matrix) {
    float rotation[3][3];
    ToRotationMatrix(q, rotation);
    *matrix = IdentityMtxf();
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            matrix->m[row][column] = rotation[row][column];
        }
    }
}

} // namespace

void Projectiles::AfterMove(PlacedObject& object) {
    // chrobjCollisionRelated, setupUpdateObjectRoomPosition and sub_GAME_7F0402B4.
    Objects& objects = *shots_->Scene().objects;
    objects.UpdateCollision(&object);
    objects.UpdateRooms(&object);
    objects.UpdateShading(&object);
    DetonateProximityMines(object.runtimePosition);
}

void Projectiles::KnifeSound(PlacedObject& object) {
    // objUpdateThrowKnifeSound.
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) == 0 || object.projectile < 0) {
        return;
    }
    const Projectile& projectile = shots_->Scene().objects->ProjectileAt(object.projectile);
    if ((projectile.flags & ProjectileFlag::Airborne) != 0 && static_cast<int32_t>(projectile.bounces) <= 0 &&
        (object.runtimeFlags & ObjectRuntime::ThrowingKnife) != 0) {
        // The whoosh picks one of three sounds.
        // TODO(https://github.com/liz-baker/frigate/issues/41): playing it.
        shots_->Scene().random();
    } else {
        object.runtimeFlags &= ~ObjectRuntime::ThrowingKnife;
    }
}

TickOp Projectiles::TickFlight(int propId) {
    const ShotScene& scene = shots_->Scene();
    Objects& objects = *scene.objects;
    PropList& props = *scene.props;
    PlacedObject* found = objects.ForProp(propId);
    if (found == nullptr) {
        return TickOp::None;
    }
    PlacedObject& object = *found;

    if ((object.runtimeFlags & ObjectRuntime::IsRetick) != 0) {
        object.runtimeFlags &= ~ObjectRuntime::IsRetick;
    } else if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0) {
        props[propId].flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
        object.runtimeFlags |= ObjectRuntime::IsRetick;
        return TickOp::Retick;
    }

    TickOp op = TickOp::None;
    bool moved = false;
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(object.projectile);
        projectile.age += static_cast<uint32_t>(clock_.clockTimer);
        if (projectile.age > kLifetimeTicks) {
            object.runtimeFlags |= ObjectRuntime::Remove;
        }

        const int ownerProp = projectile.ownerProp;
        const auto setMoving = [&](bool unset) {
            if (ownerProp >= 0 && hooks_.setMoving) {
                hooks_.setMoving(ownerProp, unset);
            }
        };

        if ((projectile.flags & ProjectileFlag::Blocked) != 0) {
            // The launch was blocked by something in front of the player: the
            // first move goes from where the player stood to where it aimed.
            const bool rocket = IsCollectable(object) && object.weaponNum == kItemRocketRound;
            Vec3 point{};
            Vec3 normal{};
            setMoving(false);
            const int result = Move(object, projectile.launchTarget, &point, &normal, false, !rocket);
            setMoving(true);
            if (result != 1 && rocket) {
                object.weaponTimer = 0;
            }
            projectile.flags &= ~ProjectileFlag::Blocked;
        }

        Vec3 current = object.runtimePosition;
        if (projectile.refreshRate > 0) {
            projectile.refreshRate -= clock_.clockTimer;
        }

        if ((projectile.flags & ProjectileFlag::Airborne) != 0) {
            op = TickAirborne(object, projectile, &current, &moved);
        } else {
            TickLanded(object, projectile, &current, &moved);
        }
    }

    if (moved && object.prop >= 0 && op != TickOp::ChangedList) {
        AfterMove(object);
    }
    return op;
}

TickOp Projectiles::TickAirborne(PlacedObject& object, Projectile& projectile, Vec3* currentOut, bool* moved) {
    const ShotScene& scene = shots_->Scene();
    Objects& objects = *scene.objects;
    PropList& props = *scene.props;
    const int propId = object.prop;
    Prop& prop = props[propId];
    Vec3& current = *currentOut;
    const float delta = clock_.globalTimerDelta;
    const int32_t weapon = WeaponOf(object);
    TickOp op = TickOp::None;

    const ModelBounds* bounds = objects.BoundsOf(object);
    bool hitGround = false;
    bool bounceCondition = false;
    bool stopped = false;
    float bottomOffset = 1.0f;
    const float startY = object.runtimePosition[1];
    bool canEmbed = false;
    projectile.airTicks += static_cast<uint32_t>(clock_.clockTimer);
    const Vec3 previousXAxis = { object.mtx.m[0][0], object.mtx.m[0][1], object.mtx.m[0][2] };

    if ((projectile.flags & ProjectileFlag::Rising) != 0) {
        if (projectile.lift < kGravity) {
            projectile.apexSpeed += projectile.acceleration[1] * delta;
            projectile.apexHeight += projectile.apexSpeed * delta;
            projectile.lift += (1.0f / 90.0f) * delta;
            if (projectile.lift > kGravity) {
                projectile.lift = kGravity;
            }
        } else if (current[1] < projectile.apexHeight) {
            projectile.apexSpeed += projectile.acceleration[1] * delta;
            projectile.apexHeight += projectile.apexSpeed * delta;
            current[1] += (0.07f * (projectile.apexHeight - current[1])) * delta;
        } else {
            current[1] = projectile.apexHeight;
            projectile.flags &= ~ProjectileFlag::Rising;
            projectile.lift = 0.0f;
            projectile.flags |= ProjectileFlag::Powered;
            projectile.speed[1] = projectile.apexSpeed;
        }
    }

    if ((projectile.flags & ProjectileFlag::Powered) == 0) {
        projectile.speed[1] += (projectile.acceleration[1] + projectile.lift) * delta;
        const float speedY = projectile.speed[1];
        const float next = speedY - (kGravity * delta);
        current[1] += (delta * (speedY + next)) * 0.5f;
        projectile.speed[1] = next;
    } else {
        projectile.speed[1] += (projectile.acceleration[1] + projectile.lift) * delta;
        current[1] += projectile.speed[1] * delta;
    }

    projectile.speed[0] += projectile.acceleration[0] * delta;
    projectile.speed[2] += projectile.acceleration[2] * delta;
    current[0] += projectile.speed[0] * delta;
    current[2] += projectile.speed[2] * delta;
    // sub_GAME_7F057DF8: the spin, once for each tick.
    for (int i = 0; i < clock_.clockTimer; i++) {
        object.mtx = MultiplyHomogeneous(projectile.spin, object.mtx);
    }

    // The objects that stick to what they hit.
    if (IsCollectable(object) &&
        (weapon == kItemRemoteMine || weapon == kItemTimedMine || weapon == kItemProximityMine ||
         weapon == kItemBombCase || weapon == kItemBug || weapon == kItemMicroCamera || weapon == kItemPlastique)) {
        canEmbed = true;
    }

    const int ownerProp = projectile.ownerProp;
    if (ownerProp >= 0 && hooks_.setMoving) {
        hooks_.setMoving(ownerProp, false);
    }
    Vec3 collisionPoint{};
    Vec3 collisionNormal{};
    const int moveResult = Move(object, current, &collisionPoint, &collisionNormal, canEmbed, false);
    if (ownerProp >= 0 && hooks_.setMoving) {
        hooks_.setMoving(ownerProp, true);
    }
    *moved = true;

    const int hitProp = contact_.prop;
    const bool hitBody =
        hitProp >= 0 && (props[hitProp].type == PropType::Chr || props[hitProp].type == PropType::Viewer);
    if (moveResult == 2 && !hitBody) {
        bool hitFlies = false;
        if (hitProp >= 0) {
            if (const PlacedObject* hitObject = objects.ForProp(hitProp)) {
                hitFlies = (hitObject->runtimeFlags & ObjectRuntime::HasProjectile) != 0;
            }
        }
        if (!hitFlies) {
            objects.FreeProjectile(object.projectile);
            object.projectile = -1;
            object.runtimeFlags &= ~ObjectRuntime::HasProjectile;
            if ((prop.flags & PropFlag::SeedFromRooms) != 0) {
                prop.flags |= PropFlag::KeepRoomList;
            }
            // TODO(https://github.com/liz-baker/frigate/issues/41): ATTACH_MINE_SFX.
            if (hooks_.checkDeposit && prop.stan != kNoTile) {
                hooks_.checkDeposit(object.weaponNum, scene.stan->TileRoom(prop.stan));
            }
            StickToSurface(object, collisionPoint, prop.stan, collisionNormal);
            if (hitProp >= 0) {
                const int tile = prop.stan;
                if (Embed(propId, hitProp, contact_.model, contact_.node)) {
                    prop.stan = tile;
                    op = TickOp::ChangedList;
                    stopped = true;
                }
            }
        }
    }

    if (!stopped && (object.runtimeFlags & ObjectRuntime::HasProjectile) != 0) {
        if (hitProp >= 0 && IsCollectable(object)) {
            const Prop& hit = props[hitProp];
            if (weapon == kItemThrowKnife) {
                if (hit.type == PropType::Chr) {
                    Chr& chr = (*scene.chrs)[hit.object];
                    if ((projectile.flags & ProjectileFlag::Airborne) != 0 &&
                        static_cast<int32_t>(projectile.bounces) <= 0 &&
                        (object.runtimeFlags & ObjectRuntime::ThrowingKnife) != 0 && scene.shotActor &&
                        scene.shotActor(chr, contact_.hitpart, contact_.worldDirection, weapon, true)) {
                        stopped = true;
                        if (projectile.bounce > 0.0f) {
                            float along = ((projectile.speed[0] * collisionNormal[0]) +
                                           (projectile.speed[1] * collisionNormal[1])) +
                                          (projectile.speed[2] * collisionNormal[2]);
                            along *= -(projectile.bounce + 1.0f);
                            for (size_t i = 0; i < 3; i++) {
                                projectile.speed[i] += along * collisionNormal[i];
                            }
                        }
                        if ((projectile.flags & ProjectileFlag::NoSpin) == 0) {
                            projectile.spin = RandomRotation();
                        }
                        projectile.bounces += 1;
                        shots_->DrawHitSounds();

                        if ((hit.flags & PropFlag::OnScreen) != 0 && contact_.hitpart != HitPart::Gun &&
                            contact_.hitpart != HitPart::Hat && contact_.model != nullptr) {
                            NodeRef node;
                            node.model = contact_.model;
                            node.node = contact_.node;
                            if (const Mtxf* nodeMatrix = NodeMatrix(node)) {
                                Mtxf worldToView;
                                std::memcpy(worldToView.m, scene.camera->WorldToScreen(), sizeof(worldToView.m));
                                Vec3 stain = TransformVector(worldToView, collisionPoint);
                                const Vec3 nodePosition = { nodeMatrix->m[3][0], nodeMatrix->m[3][1],
                                                            nodeMatrix->m[3][2] };
                                for (size_t i = 0; i < 3; i++) {
                                    stain[i] += (stain[i] - nodePosition[i]) * 0.5f;
                                }
                                const float size = JointSize(node);
                                for (size_t i = 0; i < 3; i++) {
                                    stain[i] -= (size * 0.5f) * contact_.viewDirection[i];
                                }
                                stain = TransformVector(InverseRotationAndTranslation(*nodeMatrix), stain);
                                shots_->BloodStain(*const_cast<Model::ModelInstance*>(contact_.model),
                                                   contact_.hitpart, node, stain);
                            }
                        }
                    }
                }
            } else if (weapon == kItemRocketRound) {
                stopped = true;
                if (hit.type == PropType::Chr) {
                    if (hooks_.damageChr) {
                        hooks_.damageChr(hitProp, object.runtimePosition, 2.0f);
                    }
                } else if (hit.type == PropType::Obj || hit.type == PropType::Weapon) {
                    if (PlacedObject* hitObject = objects.ForProp(hitProp)) {
                        const int owner = static_cast<int>((object.runtimeFlags & ObjectRuntime::OwnerMask) >>
                                                           ObjectRuntime::OwnerShift);
                        scene.damage->Apply(*hitObject, 100.0f, object.runtimePosition, kItemRocketRound, owner);
                    }
                }
                object.weaponTimer = 0;
            }
        }
    }

    if (!stopped && (object.runtimeFlags & ObjectRuntime::HasProjectile) != 0) {
        if (moveResult == 0 && projectile.bounce > 0.0f) {
            const float previousSpeedY = projectile.speed[1];
            float along = ((projectile.speed[0] * collisionNormal[0]) + (projectile.speed[1] * collisionNormal[1])) +
                          (projectile.speed[2] * collisionNormal[2]);
            along *= -(projectile.bounce + 1.0f);
            for (size_t i = 0; i < 3; i++) {
                projectile.speed[i] += along * collisionNormal[i];
            }
            if (previousSpeedY <= 0.0f && (projectile.speed[1] >= 0.0f || startY <= object.runtimePosition[1])) {
                bounceCondition = true;
            }
        }

        float floorY = 0.0f;
        if ((projectile.flags & ProjectileFlag::OffTiles) == 0) {
            floorY = scene.stan->PositionY(prop.stan, prop.position[0], prop.position[2]);
            bottomOffset = bounds != nullptr ? SumMatrixPos(*bounds, object.mtx, 1) : 0.0f;
            hitGround = prop.position[1] < (floorY - bottomOffset);
        }

        if (hitGround || moveResult == 0) {
            if ((projectile.flags & ProjectileFlag::NoSpin) == 0) {
                projectile.spin = RandomRotation();
            }
            projectile.bounces += 1;
        }

        if (hitGround || bounceCondition) {
            const float y = hitGround ? (floorY - bottomOffset) + 4.0f : (collisionPoint[1] - bottomOffset) + 4.0f;
            prop.position[1] = y;
            object.runtimePosition[1] = y;

            if ((object.runtimeFlags & ObjectRuntime::FellDestroyed) == 0) {
                object.runtimeFlags |= ObjectRuntime::DamageOnLand;
            }

            if (projectile.bounce > 0.0f) {
                projectile.speed[1] *= -projectile.bounce;
                if (projectile.speed[1] < 2.2222223f) {
                    if ((projectile.flags & ProjectileFlag::Bouncy) != 0 && projectile.bounces == 1) {
                        projectile.speed[1] = 2.2222223f;
                    } else {
                        Settle(object, previousXAxis);
                    }
                }
            } else {
                Settle(object, previousXAxis);
            }
        }

        if (IsCollectable(object)) {
            if (weapon == kItemThrowKnife) {
                KnifeSound(object);
            } else if (weapon == kItemRocketRound) {
                if (moveResult == 0 || hitGround) {
                    object.weaponTimer = 0;
                } else {
                    const float speedSquared = ((projectile.speed[0] * projectile.speed[0]) +
                                                (projectile.speed[1] * projectile.speed[1])) +
                                               (projectile.speed[2] * projectile.speed[2]);
                    if (speedSquared > kRocketSpeedLimit) {
                        projectile.acceleration = { 0.0f, 0.0f, 0.0f };
                    }
                    if (projectile.airTicks >= kGrenadeSmokeTicks) {
                        projectile.lift = 0.0f;
                        projectile.flags &= ~(ProjectileFlag::Powered | ProjectileFlag::Rising);
                    } else {
                        scene.explosions->CreateSmoke(object.runtimePosition, prop.stan, 8, prop.rooms,
                                                      (prop.flags & PropFlag::SeedFromRooms) != 0);
                    }
                }
            } else if (weapon == kItemGrenadeRound) {
                if (hitGround || bounceCondition) {
                    object.weaponTimer = 0;
                } else {
                    scene.explosions->CreateSmoke(object.runtimePosition, prop.stan, 9, prop.rooms,
                                                  (prop.flags & PropFlag::SeedFromRooms) != 0);
                }
            }

            if (moveResult == 0 || hitGround) {
                // TODO(https://github.com/liz-baker/frigate/issues/41): KNIFE_HIT_WALL_SFX and
                // DROP_GUN_SFX, played when the last impact was not just before.
                projectile.lastImpact = clock_.frame;
            }
        }
    }

    // A crashing aircraft sets off explosions in its wake.
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0 &&
        (projectile.flags & ProjectileFlag::Falling) != 0 && (clock_.frame & 7) == 0) {
        Vec3 point = { object.runtimePosition[0] + 400.0f, object.runtimePosition[1] - 1800.0f,
                       object.runtimePosition[2] };
        if ((clock_.frame & 0xF) == 0) {
            point[2] += 400.0f;
        } else {
            point[2] -= 400.0f;
        }
        scene.explosions->Create(-1, point, prop.stan, 0x14, 0, 0, prop.rooms, false, scene.random);
        if ((clock_.frame % 40) == 0) {
            scene.explosions->CreateSmoke(point, prop.stan, 0xA, prop.rooms, true);
        }
    }
    return op;
}

void Projectiles::TickLanded(PlacedObject& object, Projectile& projectile, Vec3* currentOut, bool* moved) {
    const ShotScene& scene = shots_->Scene();
    Objects& objects = *scene.objects;
    Prop& prop = (*scene.props)[object.prop];
    Vec3& current = *currentOut;
    bool alive = true;

    if (projectile.settleProgress < 1.0f) {
        projectile.settleProgress += projectile.settleRate * clock_.globalTimerDelta;
        if (clock_.clockTimer > 0) {
            projectile.settleRate *= 1.1f;
        }
        if (projectile.settleProgress > 1.0f || (projectile.flags & ProjectileFlag::OffTiles) != 0) {
            projectile.settleProgress = 1.0f;
        }
        const Quaternion q =
            Slerp(FromArray(projectile.settleFrom), FromArray(projectile.settleTo), projectile.settleProgress);
        SetRotationFromQuaternion(q, &object.mtx);
        ScaleRowBasis(&object.mtx, 0, projectile.scaleX);
        ScaleRowBasis(&object.mtx, 1, projectile.scaleY);
        ScaleRowBasis(&object.mtx, 2, projectile.scaleZ);
        alive = false;
    }

    if (((projectile.speed[0] != 0.0f || projectile.speed[2] != 0.0f) || projectile.settleProgress < 1.0f) &&
        (projectile.flags & ProjectileFlag::OffTiles) == 0) {
        const ModelBounds* bounds = objects.BoundsOf(object);
        alive = false;

        for (int i = 0; i < clock_.clockTimer; i++) {
            current[0] += projectile.speed[0];
            current[2] += projectile.speed[2];
            if (projectile.settleProgress >= 1.0f) {
                if (projectile.friction > 0.0f) {
                    const float fraction = (projectile.friction * clock_.globalTimerDelta) /
                                           std::sqrt((projectile.speed[0] * projectile.speed[0]) +
                                                     (projectile.speed[2] * projectile.speed[2]));
                    if (fraction >= 1.0f) {
                        projectile.speed[0] = 0.0f;
                        projectile.speed[2] = 0.0f;
                    } else {
                        projectile.speed[0] -= projectile.speed[0] * fraction;
                        projectile.speed[2] -= projectile.speed[2] * fraction;
                    }
                } else {
                    projectile.speed[0] *= kFriction;
                    projectile.speed[2] *= kFriction;
                }
            }
        }

        Vec3 point{};
        Vec3 normal{};
        Move(object, current, &point, &normal, false, false);
        *moved = true;
        const float floorY = scene.stan->PositionY(prop.stan, prop.position[0], prop.position[2]);
        const float y = (floorY - (bounds != nullptr ? SumMatrixPos(*bounds, object.mtx, 1) : 0.0f)) + 4.0f;
        prop.position[1] = y;
        object.runtimePosition[1] = y;
        if ((projectile.speed[0] < 0.1f && projectile.speed[0] > -0.1f) &&
            (projectile.speed[2] < 0.1f && projectile.speed[2] > -0.1f)) {
            projectile.speed[2] = 0.0f;
            projectile.speed[0] = 0.0f;
        }
    }

    if (alive || (projectile.flags & ProjectileFlag::OffTiles) != 0) {
        objects.FreeProjectile(object.projectile);
        object.projectile = -1;
        object.runtimeFlags &= ~ObjectRuntime::HasProjectile;
        if ((prop.flags & PropFlag::SeedFromRooms) != 0) {
            prop.flags |= PropFlag::KeepRoomList;
        }
    }
}

// -- Fuses and mines --------------------------------------------------------------------------

void Projectiles::AddProximityMine(int prop) {
    for (int& slot : proximityMines_) {
        if (slot < 0) {
            slot = prop;
            return;
        }
    }
}

void Projectiles::RemoveProximityMine(int prop) {
    for (int& slot : proximityMines_) {
        if (slot == prop) {
            slot = -1;
            return;
        }
    }
}

void Projectiles::DetonateProximityMines(const Vec3& position) {
    Objects& objects = *shots_->Scene().objects;
    for (const int prop : proximityMines_) {
        PlacedObject* mine = prop >= 0 ? objects.ForProp(prop) : nullptr;
        if (mine == nullptr || mine->weaponTimer != 1) {
            continue;
        }
        const float dx = position[0] - mine->runtimePosition[0];
        const float dy = position[1] - mine->runtimePosition[1];
        const float dz = position[2] - mine->runtimePosition[2];
        if ((dx * dx) + (dy * dy) + (dz * dz) < kProximityRangeSquared) {
            mine->weaponTimer = 0;
        }
    }
}

void Projectiles::Explode(int propId, int type) {
    // propExplode.
    const ShotScene& scene = shots_->Scene();
    PropList& props = *scene.props;
    PlacedObject* object = scene.objects->ForProp(propId);
    if (object == nullptr) {
        return;
    }
    Prop& prop = props[propId];
    const int player =
        static_cast<int>((object->runtimeFlags & ObjectRuntime::OwnerMask) >> ObjectRuntime::OwnerShift);
    const bool scorches = (object->flags & 0xE) == 0;

    if (prop.parent >= 0) {
        int rootId = prop.parent;
        while (props[rootId].parent >= 0) {
            rootId = props[rootId].parent;
        }
        const Prop& root = props[rootId];
        int tile = root.stan;
        Vec3 position{};
        if ((prop.flags & PropFlag::OnScreen) != 0 && object->instance) {
            const auto& matrices = static_cast<LevelModel&>(*object->instance).Instance()->Walk().matrices;
            Mtxf matrix;
            std::memcpy(matrix.m, matrices[0].m, sizeof(matrix.m));
            position = TransformVector(scene.camera->ViewToWorld(), { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] });
        } else {
            position = root.position;
        }
        if ((root.flags & PropFlag::SeedFromRooms) == 0 &&
            scene.stan->WalkTiles(&tile, root.position[0], root.position[2], position[0], position[2])) {
            scene.explosions->Create(-1, position, tile, type, scorches ? 1 : 0, player, root.rooms, false,
                                     scene.random);
        } else {
            scene.explosions->Create(-1, position, tile, type, 0, player, root.rooms, true, scene.random);
        }
    } else {
        scene.explosions->Create(-1, object->runtimePosition, prop.stan, type,
                                 (scorches && (prop.flags & PropFlag::SeedFromRooms) == 0) ? 1 : 0, player, prop.rooms,
                                 (prop.flags & PropFlag::SeedFromRooms) != 0, scene.random);
    }
}

void Projectiles::WeaponTick(int propId) {
    // chrobjWeaponTick.
    const ShotScene& scene = shots_->Scene();
    PlacedObject* found = scene.objects->ForProp(propId);
    if (found == nullptr) {
        return;
    }
    PlacedObject& object = *found;

    // An ammo crate that was shot set off.
    if (object.type == static_cast<uint8_t>(PropDefType::Magazine)) {
        if ((object.flags & ObjectFlag::DroneGun) != 0) {
            Explode(propId, kExplosionDrone);
            object.runtimeFlags |= ObjectRuntime::Remove;
        }
        return;
    }
    if (!IsCollectable(object)) {
        return;
    }

    const bool massive = (object.flags2 & ObjectFlag2::DoorAltCoordSystem) != 0;
    const int standard = massive ? kExplosionMassive : kExplosionStandard;
    const int32_t weapon = object.weaponNum;
    int16_t& timer = object.weaponTimer;

    if ((weapon == kItemGrenade || weapon == kItemGrenadeRound) && timer >= 0) {
        timer = static_cast<int16_t>(timer - clock_.clockTimer);
        if (timer < 0) {
            Explode(propId, standard);
            object.runtimeFlags |= ObjectRuntime::Remove;
        }
    } else if (weapon == kItemRocketRound) {
        if (timer == 0) {
            Explode(propId, standard);
            object.runtimeFlags |= ObjectRuntime::Remove;
        }
    } else if (weapon == kItemPlastique) {
        if (timer == 0) {
            Explode(propId, kExplosionMassive);
            object.runtimeFlags |= ObjectRuntime::Remove;
            if (hooks_.surroundBond) {
                hooks_.surroundBond(kPlastiqueDelay);
            }
        }
    } else if ((weapon == kItemTimedMine || weapon == kItemBombCase) && timer >= 0) {
        timer = static_cast<int16_t>(timer - clock_.clockTimer);
        if (timer < 0) {
            Explode(propId, standard);
            timer = -1;
            object.runtimeFlags |= ObjectRuntime::Remove;
        }
    } else if (weapon == kItemRemoteMine) {
        if (remoteMineTrigger_ != 0) {
            const unsigned owner = (object.runtimeFlags & ObjectRuntime::OwnerMask) >> ObjectRuntime::OwnerShift;
            if ((remoteMineTrigger_ & (1U << owner)) != 0) {
                timer = 0;
            }
        }
        if (timer > 1) {
            timer = static_cast<int16_t>(timer - clock_.clockTimer);
            if (timer < 2) {
                timer = 1;
            }
        } else if (timer == 0) {
            if (massive) {
                Explode(propId, kExplosionMassive);
            } else if (hooks_.stage && hooks_.stage() == LevelId::Facility) {
                Explode(propId, kExplosionFacilityRemote);
            } else {
                Explode(propId, kExplosionStandard);
            }
            timer = -1;
            object.runtimeFlags |= ObjectRuntime::Remove;
        }
    } else if (weapon == kItemProximityMine) {
        if (timer > 1) {
            timer = static_cast<int16_t>(timer - clock_.clockTimer);
            if (timer < 2) {
                timer = 1;
                AddProximityMine(propId);
            }
        } else if (timer == 1) {
            const Prop& player = (*scene.props)[scene.playerProp()];
            const Prop& prop = (*scene.props)[propId];
            const float dx = player.position[0] - prop.position[0];
            const float dy = player.position[1] - prop.position[1];
            const float dz = player.position[2] - prop.position[2];
            if ((dx * dx) + (dy * dy) + (dz * dz) < kProximityRangeSquared) {
                timer = 0;
            }
        }
        if (timer == 0) {
            Explode(propId, standard);
            timer = -1;
            object.runtimeFlags |= ObjectRuntime::Remove;
            RemoveProximityMine(propId);
        }
    }
}

// -- Falling objects --------------------------------------------------------------------------

void Projectiles::DropRecursively(int propId) {
    // objDropRecursively.
    PropList& props = *shots_->Scene().props;
    for (int child = props[propId].child; child >= 0;) {
        const int previous = props[child].prev;
        DropRecursively(child);
        if (hooks_.dropObject) {
            hooks_.dropObject(child);
        }
        child = previous;
    }
}

void Projectiles::Fall(PlacedObject& object, int player) {
    // objFall.
    Objects& objects = *shots_->Scene().objects;
    object.runtimeFlags &= ~ObjectRuntime::OwnerMask;
    object.runtimeFlags |= static_cast<uint32_t>(player) << ObjectRuntime::OwnerShift;

    if ((object.flags2 & ObjectFlag2::NoFall) == 0 && (object.flags & ObjectFlag::Fall) != 0 &&
        (object.runtimeFlags & (ObjectRuntime::Embedded | ObjectRuntime::HasProjectile)) == 0) {
        Vec3 rotation{};
        objects.GiveProjectile(object.prop);
        Projectile* projectile = nullptr;
        if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0) {
            projectile = &objects.ProjectileAt(object.projectile);
        }
        if (projectile != nullptr) {
            projectile->speed[0] = RandomFraction() * 1.6666666f - 0.8333333f;
            projectile->speed[1] = RandomFraction() * 1.6666666f * 2.0f + 1.6666666f;
            projectile->speed[2] = RandomFraction() * 1.6666666f - 0.8333333f;

            if ((object.flags2 & ObjectFlag2::FallWithoutRotation) == 0) {
                for (float& angle : rotation) {
                    angle = ((RandomFraction() * kTau) / 320.0f) - 0.009817477f;
                }
            }
            projectile->spin = RotationXYZ(rotation);
            projectile->flags |= ProjectileFlag::Airborne;
            object.flags &= ~ObjectFlag::ForceCollisions;
            object.runtimeFlags &= ~ObjectRuntime::RestingOnObject;
        }
    }
}

void Projectiles::Bounce(PlacedObject& object, const Vec3& viewDirection) {
    // objBounce.
    Objects& objects = *shots_->Scene().objects;
    objects.GiveProjectile(object.prop);
    Projectile* projectile = objects.ProjectileOf(object);
    if (projectile == nullptr) {
        return;
    }
    projectile->speed[0] = (RandomFraction() * 1.6666666f * 4.0f) - 3.3333333f;
    projectile->speed[1] = (RandomFraction() * 1.6666666f * 2.0f) + 3.3333333f;
    projectile->speed[2] = (RandomFraction() * 1.6666666f * 4.0f) - 3.3333333f;

    Vec3 rotation{};
    for (float& angle : rotation) {
        angle = (RandomFraction() * kTau * 0.015625f) - 0.049087387f;
    }
    projectile->spin = RotationXYZ(rotation);
    projectile->flags |= ProjectileFlag::Airborne;

    const Vec3 direction = RotateVector(shots_->Scene().camera->ViewToWorld(), viewDirection);
    projectile->speed[0] += 3.3333333f * direction[0];
    projectile->speed[2] += 3.3333333f * direction[2];
    projectile->ownerProp = shots_->Scene().playerProp();
    projectile->bounces = 1;
}

void Projectiles::DestroySupported(int tableProp, int player) {
    // objDestroySupportedObjects.
    const ShotScene& scene = shots_->Scene();
    PropList& props = *scene.props;
    Objects& objects = *scene.objects;
    const Prop& table = props[tableProp];
    const PlacedObject* tableObject = objects.ForProp(tableProp);
    if (tableObject == nullptr || table.stan == kNoTile) {
        return;
    }
    const int room = scene.stan->TileRoom(table.stan);

    const CollisionBlock* block = objects.Collision(tableProp);
    if (block == nullptr || block->edges <= 0) {
        return;
    }
    const float tableY = tableObject->runtimePosition[1];
    const std::array<Vec2, 8> polygon = block->polygon;
    const int edges = block->edges;

    for (int prop = props.ActiveTail(); prop >= 0;) {
        const int before = props.ActiveBefore(prop);
        const Prop& record = props[prop];
        if ((record.type == PropType::Obj || record.type == PropType::Weapon) && record.stan != kNoTile &&
            scene.stan->TileRoom(record.stan) == room) {
            if (PlacedObject* other = objects.ForProp(prop)) {
                if (tableY < other->runtimePosition[1] &&
                    (other->runtimeFlags & ObjectRuntime::RestingOnObject) != 0 &&
                    PointInPolygon(other->runtimePosition, polygon, edges)) {
                    Fall(*other, player);
                }
            }
        }
        prop = before;
    }
}

} // namespace Frigate::Level
