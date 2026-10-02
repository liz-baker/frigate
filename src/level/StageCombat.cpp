#include <cmath>
#include <cstring>

#include "level/LevelModels.h"
#include "level/Matrix.h"
#include "level/StageAi.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
// CHRLV_DEFAULT_TIMER.
constexpr int kDefaultTimer = 180;
// RANDOMFRAC: randomGetNext() scaled to 0 to 1.
float RandomFrac() {
    return static_cast<float>(RandomGetNext()) * 2.3283064e-10f;
}

// The speed an item leaves the hand at is in these units per tick.
constexpr float kDropUnit = 1.6666666f;

} // namespace

// -- What the characters' combat reaches in the stage ---------------------------------------

const WeaponTable* Stage::AiBridge::Weapons() const {
    return stage_.weapons_.Ready() ? &stage_.weapons_.Table() : nullptr;
}

float Stage::AiBridge::AiAccuracy() const {
    return stage_.modifiers_.aiAccuracy;
}

float Stage::AiBridge::AiDamage() const {
    return stage_.modifiers_.aiDamage;
}

float Stage::AiBridge::AiHealth() const {
    return stage_.modifiers_.aiHealth;
}

float Stage::AiBridge::AiReactionSpeed() const {
    return stage_.modifiers_.aiReactionSpeed;
}

float Stage::AiBridge::PlayerDuckingHeight() const {
    return stage_.player_.DuckingHeightRelated();
}

const Mtxf& Stage::AiBridge::ViewToWorld() const {
    return stage_.camera_.ViewToWorld();
}

const Mtxf& Stage::AiBridge::PreviousViewToWorld() const {
    return stage_.camera_.PreviousViewToWorld();
}

bool Stage::AiBridge::PlayerDamageShowing() const {
    return stage_.health_.DamageShowing();
}

void Stage::AiBridge::DamagePlayer(float damage, float angle) {
    stage_.RecordPlayerDamage(damage, std::sin(angle), std::cos(angle), true);
}

void Stage::AiBridge::CountPlayerShot(int item, int shotRegister) {
    stage_.shots_.CountShot(item, shotRegister);
}

void Stage::AiBridge::DrawHitSounds() {
    // The picks of the ricochet and surface sounds.
    // TODO(https://github.com/liz-baker/frigate/issues/41): the sounds themselves
    // (recall_joy2_hits_edit_detail_edit_flag and recall_joy2_hits_edit_flag).
    RandomGetNext();
    RandomGetNext();
}

void Stage::AiBridge::CreateSpark(const Vec3& position, int type, float size, int room) {
    stage_.sparks_.Create(position, type, size, room, RandomGetNext);
}

void Stage::AiBridge::ShootObject(PlacedObject& object, float damage, const Vec3& position, int item) {
    stage_.damage_.ApplyUnlessImmune(object, damage, position, item, 0);
}

void Stage::AiBridge::CountCivilianCasualty() {
    stage_.civilianCasualties_++;
}

int Stage::AiBridge::CiviliansKilled() const {
    return stage_.civilianCasualties_;
}

// -- Items characters let go of ------------------------------------------------------------

void Stage::AiBridge::DropProp(int prop, int how) {
    stage_.SetDropped(prop, how);
}

void Stage::AiBridge::DropConcealedItems(Chr& chr) {
    // chrDropItems: what the character carries without holding it is set to
    // fall, unless it is told to be kept.
    PropList& props = stage_.objects_.Props();
    for (int child = props[chr.prop].child; child >= 0; child = props[child].prev) {
        if (child == chr.hat || child == chr.weaponsHeld[0] || child == chr.weaponsHeld[1]) {
            continue;
        }
        const PlacedObject* item = stage_.objects_.ForProp(child);
        if (item != nullptr && (item->flags & ObjectFlag::AiUndroppable) == 0) {
            stage_.SetDropped(child, DropType::Default);
        }
    }
    chr.hidden |= ChrHidden::DropHeldItems;
}

bool Stage::AiBridge::DropNewItem(Chr& chr, int model, int item) {
    // chrDropItem: the item is made, carried and set to fall.
    if (!stage_.objects_.GiveWeapon(HostOf(chr), chr.chrnum, model, item, ObjectFlag::ConcealGun)) {
        return false;
    }
    PropList& props = stage_.objects_.Props();
    const int made = props[chr.prop].child;
    PlacedObject* object = made >= 0 ? stage_.objects_.ForProp(made) : nullptr;
    if (object == nullptr) {
        return false;
    }
    object->weaponTimer = kDefaultTimer;
    stage_.SetDropped(made, DropType::Default);
    chr.hidden |= ChrHidden::DropHeldItems;
    return true;
}

bool Stage::AiBridge::KeyDropped(int key) const {
    // weaponFindThrown: the newest prop that is, or holds, a weapon of that
    // number lying with no projectile.
    const PropList& props = stage_.objects_.Props();
    const auto check = [&](auto&& self, int propId) -> bool {
        const Prop& prop = props[propId];
        if (prop.type == PropType::Weapon) {
            const PlacedObject* object = stage_.objects_.ForProp(propId);
            if (object != nullptr && object->weaponNum == key &&
                (object->runtimeFlags & ObjectRuntime::HasProjectile) == 0) {
                return true;
            }
        }
        for (int child = prop.child; child >= 0; child = props[child].prev) {
            if (self(self, child)) {
                return true;
            }
        }
        return false;
    };
    const auto& active = props.Active();
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        if (check(check, *it)) {
            return true;
        }
    }
    return false;
}

void Stage::SetDropped(int propId, int dropType) {
    // propobjSetDropped.
    PropList& props = objects_.Props();
    if (props[propId].parent < 0) {
        return;
    }
    PlacedObject* object = objects_.ForProp(propId);
    if (object == nullptr) {
        return;
    }
    objects_.GiveProjectile(propId);
    if ((object->runtimeFlags & ObjectRuntime::Embedded) != 0 && object->embedmentProjectile >= 0) {
        objects_.ProjectileAt(object->embedmentProjectile).dropType = dropType;
    } else if ((object->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object->projectile >= 0) {
        objects_.ProjectileAt(object->projectile).dropType = dropType;
    }
}

void Stage::DropObject(int propId) {
    // objDrop.
    PropList& props = objects_.Props();
    Prop& prop = props[propId];
    PlacedObject* object = objects_.ForProp(propId);
    const int parentId = prop.parent;
    if (object == nullptr) {
        return;
    }
    if ((object->runtimeFlags & ObjectRuntime::Embedded) != 0 && object->embedmentProjectile >= 0) {
        object->projectile = object->embedmentProjectile;
        object->embedmentProjectile = -1;
        objects_.FreeEmbedment();
        object->hasEmbedment = false;
        object->runtimeFlags &= ~ObjectRuntime::Embedded;
        object->runtimeFlags |= ObjectRuntime::HasProjectile;
    }
    if (parentId < 0 || (object->runtimeFlags & ObjectRuntime::HasProjectile) == 0 || object->projectile < 0) {
        return;
    }

    Projectile& projectile = objects_.ProjectileAt(object->projectile);
    const Prop& parent = props[parentId];

    projectile.flags |= ProjectileFlag::Airborne;
    projectile.ownerProp = parentId;

    if (projectile.dropType == DropType::Surrender && parent.type == PropType::Chr) {
        const float angle = ChrHeading(chrs_[parent.object]);
        projectile.speed[0] = std::sin(angle) * kDropUnit;
        projectile.speed[1] = -RandomFrac() * kDropUnit * 0.5f;
        projectile.speed[2] = std::cos(angle) * kDropUnit;

        Vec3 rotation{};
        rotation[0] = (RandomFrac() * kTau * 0.0078125f) - 0.024543693f;
        rotation[1] = (RandomFrac() * kTau * 0.0078125f) - 0.024543693f;
        rotation[2] = (RandomFrac() * kTau * 0.0078125f) - 0.024543693f;
        projectile.spin = RotationXYZ(rotation);
    } else if (projectile.dropType == DropType::ThrowGrenade && parent.type == PropType::Chr) {
        const float angle = ChrHeading(chrs_[parent.object]);
        projectile.speed[0] = std::sin(angle) * 13.333333f;
        projectile.speed[1] = 6.6666665f;
        projectile.speed[2] = std::cos(angle) * 13.333333f;

        Vec3 rotation{};
        rotation[0] = (RandomFrac() * kTau * 0.0078125f) - 0.024543693f;
        rotation[1] = (RandomFrac() * kTau * 0.0078125f) - 0.024543693f;
        rotation[2] = (RandomFrac() * kTau * 0.0078125f) - 0.024543693f;
        projectile.spin = RotationXYZ(rotation);
        projectile.flags |= ProjectileFlag::Thrown;
    } else if (projectile.dropType == DropType::Hat) {
        const Prop& player = props[player_.PropId()];
        const float x = parent.position[0] - player.position[0];
        const float z = parent.position[2] - player.position[2];
        const float angle = std::atan2(x, z);

        projectile.speed[0] = ((2.0f * (RandomFrac() * kDropUnit)) + 3.3333333f) * std::sin(angle);
        projectile.speed[1] = 2.0f * (RandomFrac() * kDropUnit);
        projectile.speed[2] = ((2.0f * (RandomFrac() * kDropUnit)) + 3.3333333f) * std::cos(angle);

        Vec3 rotation{};
        rotation[0] = (RandomFrac() * kTau * 0.03125f) - 0.09817477f;
        rotation[1] = (RandomFrac() * kTau * 0.03125f) - 0.09817477f;
        rotation[2] = (RandomFrac() * kTau * 0.03125f) - 0.09817477f;
        projectile.spin = RotationXYZ(rotation);
    } else {
        // sub_GAME_7F057C14 (DROPTYPE_OWNERREAP, or the default).
        projectile.speed[0] = RandomFrac() * kDropUnit * 4.0f - 3.3333333f;
        projectile.speed[1] = RandomFrac() * kDropUnit * 4.0f;
        projectile.speed[2] = RandomFrac() * kDropUnit * 4.0f - 3.3333333f;

        // mtxLoadRandomRotation.
        Vec3 rotation{};
        rotation[0] = RandomFrac() * 7.53982257f * 0.03125f - 0.11780973f;
        rotation[1] = RandomFrac() * 7.53982257f * 0.03125f - 0.11780973f;
        rotation[2] = RandomFrac() * 7.53982257f * 0.03125f - 0.11780973f;
        projectile.spin = RotationXYZ(rotation);
    }

    int rootId = parentId;
    while (props[rootId].parent >= 0) {
        rootId = props[rootId].parent;
    }
    const Prop& root = props[rootId];
    int rootStan = root.stan;

    // A weapon is wider than the other objects.
    const float width = prop.type == PropType::Weapon ? 20.0f : 10.0f;
    Mtxf placed;
    Chr* rootChr = root.type == PropType::Chr && root.object >= 0 ? &chrs_[root.object] : nullptr;

    if ((prop.flags & PropFlag::OnScreen) != 0 && object->instance) {
        // The item starts where its model was last drawn.
        const auto& matrices = static_cast<LevelModel&>(*object->instance).Instance()->Walk().matrices;
        Mtxf view{};
        std::memcpy(view.m, matrices[0].m, sizeof(view.m));
        placed = MultiplyHomogeneous(camera_.ViewToWorld(), view);

        int cdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;
        if ((projectile.flags & ProjectileFlag::Thrown) != 0) {
            cdTypes = CdType::Objs | CdType::Players | CdType::Chrs | CdType::PathBlocker;
        }

        if (rootChr != nullptr) {
            ChrAi::SetMoving(*rootChr, false);
        }
        int tile = rootStan;
        if (level_.StanFile().TestLineUnobstructed(&tile, root.position[0], root.position[2], placed.m[3][0],
                                                   placed.m[3][2], cdTypes, 0.0f, 1.0f, 0.0f, 1.0f) != 0 &&
            level_.StanFile().TestVolume(&tile, placed.m[3][0], placed.m[3][2], width, cdTypes, 0.0f, 1.0f) < 0) {
            rootStan = tile;
        } else {
            rootStan = root.stan;
            placed.m[3][0] = root.position[0];
            placed.m[3][2] = root.position[2];
        }
        if (rootChr != nullptr) {
            ChrAi::SetMoving(*rootChr, true);
        }
        prop.zDepth = -view.m[3][2];
    } else {
        // Nothing is tested for an item out of sight.
        placed = IdentityMtxf();
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                placed.m[row][column] *= object->scale;
            }
        }
        placed.m[3][0] = root.position[0];
        placed.m[3][1] = root.position[1];
        placed.m[3][2] = root.position[2];
    }

    // objDetach.
    ReleaseHeld(propId);
    props.Detach(propId);
    if (object->instance) {
        object->instance->Detach();
    }
    object->runtimeFlags &= ~ObjectRuntime::HasOwner;

    props.Activate(propId);
    props.Enable(propId);

    const Vec3 position = { placed.m[3][0], placed.m[3][1], placed.m[3][2] };
    placed.m[3][0] = 0.0f;
    placed.m[3][1] = 0.0f;
    placed.m[3][2] = 0.0f;
    objects_.Place(object, position, placed, rootStan);
}

} // namespace Frigate::Level

namespace Frigate::Level {

void Stage::ReleaseHeld(int propId) {
    // objDetach's part for a held item: the character that held it lets go.
    PropList& props = objects_.Props();
    const Prop& parent = props[props[propId].parent];
    if (parent.type == PropType::Chr && parent.object >= 0) {
        Chr& holder = chrs_[parent.object];
        if (propId == holder.hat) {
            holder.hat = -1;
        } else if (propId == holder.weaponsHeld[HandRight]) {
            aiActions_->StopFiringHand(holder, HandRight);
            holder.weaponsHeld[HandRight] = -1;
        } else if (propId == holder.weaponsHeld[HandLeft]) {
            aiActions_->StopFiringHand(holder, HandLeft);
            holder.weaponsHeld[HandLeft] = -1;
        }
    }
}

} // namespace Frigate::Level
