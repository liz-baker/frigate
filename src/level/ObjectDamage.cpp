#include "level/ObjectDamage.h"

#include "level/Inventory.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

// AMMOTYPE values the damage test names.
constexpr int kAmmoGrenade = 5;
constexpr int kAmmoRockets = 6;
constexpr int kAmmoRemoteMine = 7;
constexpr int kAmmoProximityMine = 8;
constexpr int kAmmoTimedMine = 9;
constexpr int kAmmoGrenadeRound = 11;
constexpr int kAmmoExplosivePen = 15;
constexpr int kAmmoBombCase = 16;
constexpr int kAmmoDynamite = 19;
// AMMOTYPE_GLOBAL_MAX.
constexpr uint32_t kAmmoTypeCount = 13;

// A shot hit that is of the tick's weapon, not a blow.
constexpr float kDamageScale = 250.0f;

bool Is(const PlacedObject& object, PropDefType type) {
    return object.type == static_cast<uint8_t>(type);
}

// The weapons that go off when damaged.
bool GoesOffWhenDamaged(int32_t weapon) {
    return weapon == kItemGrenade || weapon == kItemTimedMine || weapon == kItemRemoteMine ||
           weapon == kItemProximityMine || weapon == kItemRocketRound || weapon == kItemGrenadeRound ||
           weapon == kItemBombCase || weapon == kItemPlastique;
}

} // namespace

void ObjectDamage::Apply(PlacedObject& object, float damage, const Vec3& position, int item, int player) {
    object.runtimeFlags &= ~ObjectRuntime::OwnerMask;
    object.runtimeFlags |= static_cast<uint32_t>(player) << ObjectRuntime::OwnerShift;

    if (Is(object, PropDefType::GasReleasing) && Objects::DestroyedLevel(object) == 1) {
        return;
    }

    bool apply = false;
    if (item == kItemUnarmed) {
        if (Objects::IsCollectable(object.type)) {
            if ((object.flags & ObjectFlag::HittableCollectable) == 0) {
                return;
            }
        } else if ((object.flags & ObjectFlag::UnhittableObject) != 0) {
            return;
        }
        apply = true;
    }

    if (!apply) {
        if ((object.flags & ObjectFlag::Invincible) != 0) {
            return;
        }

        if (Is(object, PropDefType::Collectable)) {
            // A weapon that goes off when damaged has its fuse run out at once.
            if (GoesOffWhenDamaged(object.weaponNum)) {
                object.weaponTimer = 0;
            }
            return;
        }

        if (Is(object, PropDefType::Magazine)) {
            const int ammo = object.ammoType;
            if (ammo == kAmmoGrenade || ammo == kAmmoRockets || ammo == kAmmoRemoteMine || ammo == kAmmoProximityMine ||
                ammo == kAmmoTimedMine || ammo == kAmmoGrenadeRound || ammo == kAmmoExplosivePen ||
                ammo == kAmmoBombCase || ammo == kAmmoDynamite) {
                object.flags |= 0x10000000;
            }
            return;
        }

        if (!Objects::IsMortal(object)) {
            return;
        }
    }

    // A whole object collects damage (times 250) towards its health. Once
    // destroyed, maxdamage counts the shots it takes instead, and four of them
    // take it to the next destroyed level.
    if (Objects::DestroyedLevel(object) == 0) {
        object.maxDamage += damage * kDamageScale;
    } else {
        const int shotsModulo = Objects::ShotsTaken(object) % 4;
        const float untilNextLevel = static_cast<float>(4 - shotsModulo);
        if (untilNextLevel < damage) {
            damage = untilNextLevel;
        } else if (damage < 1.0f) {
            damage = 1.0f;
        }
        object.maxDamage += damage;
    }

    if (Is(object, PropDefType::Glass) || Is(object, PropDefType::TintedGlass)) {
        if (object.damage <= object.maxDamage) {
            ShatterGlass(object);
        }
    } else {
        if (hooks_.setDropped) {
            hooks_.setDropped(object.prop);
        }
        Explode(object, position, player);
    }

    if (Is(object, PropDefType::Ammo)) {
        if (Objects::DestroyedLevel(object) == 1) {
            // The crate drops one of the kinds of ammo it holds.
            const uint32_t start = random_() % kAmmoTypeCount;
            uint32_t slot = start;
            do {
                const PropDef::AmmoSlot& held = object.ammoSlots[slot];
                if (held.quantity > 0 && held.model != 0xFFFF) {
                    const int crate = objects_.CreateAmmoCrate(held.model, slot);
                    if (crate >= 0) {
                        objects_.Props().Reparent(crate, object.prop);
                    }
                    break;
                }
                slot = (slot + 1) % kAmmoTypeCount;
            } while (slot != start);
        }
    }

    // A drone gun, camera or monitor stops working when destroyed.
    if (Is(object, PropDefType::Autogun)) {
        object.flags |= ObjectFlag::WeaponNoAmmo;
        if (Objects::DestroyedLevel(object) == 1) {
            object.flags |= ObjectFlag::DroneGun;
        }
    } else if (Is(object, PropDefType::Cctv)) {
        if (Objects::DestroyedLevel(object) == 1) {
            object.flags |= ObjectFlag::CctvDisabled;
        }
    } else if (Is(object, PropDefType::Monitor) || Is(object, PropDefType::MultiMonitor)) {
        if (Objects::DestroyedLevel(object) == 1 && hooks_.blackenMonitor) {
            hooks_.blackenMonitor(object);
        }
    } else if (Is(object, PropDefType::GasReleasing)) {
        if (Objects::DestroyedLevel(object) == 1 && hooks_.releaseGas) {
            hooks_.releaseGas(object.runtimePosition);
        }
    } else if (Is(object, PropDefType::Armour)) {
        // The damage an armour takes comes off the armour it gives.
        if (Objects::DestroyedLevel(object) == 0) {
            object.armourAmount = object.armourAmount * (object.damage - object.maxDamage) / object.damage;
        } else {
            object.armourAmount = 0.0f;
        }
    }

    // A destroyed object lets go of what it holds.
    if (Objects::DestroyedLevel(object) == 1 && hooks_.setDropped) {
        PropList& props = objects_.Props();
        for (int child = props[object.prop].child; child >= 0;) {
            const int next = props[child].prev;
            hooks_.setDropped(child);
            child = next;
        }
    }
}

void ObjectDamage::Detonate(int prop, float damage, const Vec3& position, int item, int owner) {
    PlacedObject* object = objects_.ForProp(prop);
    if (object == nullptr) {
        // A door takes no damage of its own (it is not mortal), but the objects
        // on it do.
        uint32_t* door = hooks_.doorRuntimeFlags ? hooks_.doorRuntimeFlags(prop) : nullptr;
        if (door == nullptr) {
            return;
        }
        *door &= ~ObjectRuntime::OwnerMask;
        *door |= static_cast<uint32_t>(owner) << ObjectRuntime::OwnerShift;
        if ((*door & ObjectRuntime::HasOwner) == 0) {
            PropList& props = objects_.Props();
            for (int child = props[prop].child; child >= 0;) {
                const int next = props[child].prev;
                Detonate(child, damage, position, item, owner);
                child = next;
            }
        }
        return;
    }
    object->runtimeFlags &= ~ObjectRuntime::OwnerMask;
    object->runtimeFlags |= static_cast<uint32_t>(owner) << ObjectRuntime::OwnerShift;

    if ((object->runtimeFlags & ObjectRuntime::HasOwner) == 0) {
        PropList& props = objects_.Props();
        for (int child = props[prop].child; child >= 0;) {
            const int next = props[child].prev;
            Detonate(child, damage, position, item, owner);
            child = next;
        }
        Apply(*object, damage, position, item, owner);
    }
}

void ObjectDamage::ApplyUnlessImmune(PlacedObject& object, float damage, const Vec3& position, int item, int player) {
    if ((object.flags2 & ObjectFlag2::ImmuneToGunfire) == 0) {
        Apply(object, damage, position, item, player);
    }
}

void ObjectDamage::ShatterGlass(PlacedObject& object) {
    const ModelBounds* bounds = objects_.BoundsOf(object);
    impacts_.ClearByFlag(object.prop, 0);
    impacts_.ClearByFlag(object.prop, 1);

    if (bounds != nullptr) {
        const Mtxf& matrix = object.mtx;
        shards_.ShatterWindow(object.runtimePosition, { matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] },
                              { matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] },
                              { matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] }, bounds->xmin, bounds->xmax,
                              bounds->ymin, bounds->ymax, random_);
    }

    object.runtimeFlags |= ObjectRuntime::Remove;
    object.state |= ObjectState::Destroyed;
    object.maxDamage = 0.0f;
}

void ObjectDamage::Explode(PlacedObject& object, const Vec3& position, int player) {
    if (!(object.damage < object.maxDamage)) {
        if (Objects::DestroyedLevel(object) == 0) {
            return;
        }
    }

    PropList& props = objects_.Props();
    const int prop = object.prop;
    const PropModel* model = objects_.Model(object.model);
    const int explosionType = model != nullptr ? model->explosion : 0;

    int tail = prop;
    while (props[tail].parent >= 0) {
        tail = props[tail].parent;
    }
    int tile = props[tail].stan;
    const Prop& tailProp = props[tail];

    // Where the explosion stands: on the tile the object's topmost holder is
    // on, if the shot can be traced from there, else seeded from the holder's
    // rooms.
    const auto create = [&](int type, int scorches) {
        if (tile == kNoTile) {
            return;
        }
        if ((tailProp.flags & PropFlag::SeedFromRooms) == 0 &&
            stan_.WalkTiles(&tile, tailProp.position[0], tailProp.position[2], position[0], position[2])) {
            explosions_.Create(prop, position, tile, type, scorches, player, tailProp.rooms, false, random_);
        } else {
            explosions_.Create(prop, position, tailProp.stan, type, 0, player, tailProp.rooms, true, random_);
        }
    };

    if (Objects::DestroyedLevel(object) == 0) {
        object.state |= ObjectState::Destroyed;
        object.maxDamage = 0.0f;

        create(explosionType, (object.flags & 0xE) == 0 ? 1 : 0);

        if ((object.flags2 & ObjectFlag2::RemoveWhenDestroyed) != 0) {
            object.runtimeFlags |= ObjectRuntime::Remove;
            return;
        }

        if (hooks_.deform) {
            hooks_.deform(object, 1);
        }
        if (tail != prop) {
            return;
        }
        if (hooks_.destroySupported) {
            hooks_.destroySupported(prop, player);
        }
        if ((object.runtimeFlags & ObjectRuntime::RestingOnObject) != 0) {
            if ((random_() % 3) != 0) {
                return;
            }
        }
        object.runtimeFlags |= ObjectRuntime::FellDestroyed;
        if (hooks_.fall) {
            hooks_.fall(object, player);
        }
        return;
    }

    const int shots = Objects::ShotsTaken(object);
    if ((shots & 3) == 0) {
        if (hooks_.deform) {
            hooks_.deform(object, (shots >> 2) + 1);
        }
        create(0x10, (object.flags & 0xE) == 0 ? 1 : 0);
    }

    if (Objects::DestroyedLevel(object) > 0 && (object.state & ObjectState::Respawn) != 0) {
        if ((object.runtimeFlags & ObjectRuntime::Regenerating) != 0) {
            object.state |= ObjectState::State10;
        } else {
            object.state &= static_cast<uint8_t>(~ObjectState::State10);
        }
        props[prop].timeToRegen = 1200;
    }

    if (shots >= 12) {
        object.runtimeFlags |= ObjectRuntime::Regenerating;
        object.flags &= ~ObjectFlag::ForceCollisions;
    }
}

} // namespace Frigate::Level
