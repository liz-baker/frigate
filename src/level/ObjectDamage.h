#pragma once

#include <functional>

#include "level/BulletImpacts.h"
#include "level/Explosions.h"
#include "level/GlassShards.h"
#include "level/Objects.h"
#include "level/Stan.h"

namespace Frigate::Level {

// What an object's damage reaches into the rest of the stage for.
struct ObjectDamageHooks {
    // propobjSetDropped(prop, DROPTYPE_DEFAULT): lets go of an object held by
    // another.
    std::function<void(int prop)> setDropped;
    // objDeform: the object is blown out of shape for the damage of `type` (1 to
    // 3).
    std::function<void(PlacedObject& object, int type)> deform;
    // objDestroySupportedObjects and objFall.
    std::function<void(int prop, int player)> destroySupported;
    std::function<void(PlacedObject& object, int player)> fall;
    // init_trigger_toxic_gas_effect at the object's position.
    std::function<void(const Vec3& position)> releaseGas;
    // A monitor's screen goes black (monAnim33BlackSolid).
    std::function<void(PlacedObject& monitor)> blackenMonitor;
};

// objApplyDamage and what follows from it (propobj.c): an object takes the
// damage of a shot or an explosion, is destroyed, and breaks, explodes or
// stops working.
class ObjectDamage {
  public:
    using RandomFn = std::function<uint32_t()>;

    ObjectDamage(Objects& objects, Stan& stan, BulletImpacts& impacts, GlassShards& shards, Explosions& explosions)
        : objects_(objects), stan_(stan), impacts_(impacts), shards_(shards), explosions_(explosions) {
    }

    void SetHooks(ObjectDamageHooks hooks) {
        hooks_ = std::move(hooks);
    }
    void SetRandom(RandomFn random) {
        random_ = std::move(random);
    }

    // objApplyDamage: `item` is the weapon that did the damage (kItemUnarmed
    // for a blow) and `player` the player it is credited to.
    void Apply(PlacedObject& object, float damage, const Vec3& position, int item, int player);
    // maybe_detonate_object_and_its_children: the damage goes to the object
    // `prop` is of and, first, to the objects on it.
    void Detonate(int prop, float damage, const Vec3& position, int item, int owner);
    // chrobjMaybeDetonateObjectIfFlags: not for an object immune to gunfire.
    void ApplyUnlessImmune(PlacedObject& object, float damage, const Vec3& position, int item, int player);
    // sub_GAME_7F04DCB4: a glass object breaks into shards.
    void ShatterGlass(PlacedObject& object);
    // objExplode.
    void Explode(PlacedObject& object, const Vec3& position, int player);

  private:
    Objects& objects_;
    Stan& stan_;
    BulletImpacts& impacts_;
    GlassShards& shards_;
    Explosions& explosions_;
    ObjectDamageHooks hooks_;
    RandomFn random_;
};

} // namespace Frigate::Level
