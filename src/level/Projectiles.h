#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "level/BgLevel.h"
#include "level/Explosions.h"
#include "level/ObjectDamage.h"
#include "level/Objectives.h"
#include "level/Objects.h"
#include "level/Shots.h"
#include "level/Stan.h"

namespace Frigate::Level {

// What a prop's tick asks the prop list to do next (TICKOP_*).
enum class TickOp {
    None,
    Free,
    // The prop is ticked again at the end of the frame's walk.
    Retick,
    // The tick changed the prop list: carry on from the prop before it.
    ChangedList,
};

// g_ClockTimer, g_GlobalTimerDelta, g_GlobalTimer and D_80048380.
struct TickClock {
    int clockTimer = 1;
    float globalTimerDelta = 1.0f;
    int globalTimer = 0;
    int frame = 0;
};

// What the projectiles reach into the rest of the stage for.
struct ProjectileHooks {
    // chrlvExplosionDamage(chr, position, damage, 1).
    std::function<void(int prop, const Vec3& position, float damage)> damageChr;
    // sub_GAME_7F03D058 for a prop: whether it is in the way of tests (`unset`
    // takes it out of them for as long as it is false).
    std::function<void(int prop, bool unset)> setMoving;
    // objDrop: the item a character let go of falls.
    std::function<void(int prop)> dropObject;
    // Where each character that moves unseen is (chrlvGetPatrolPercentOrPosition),
    // for the proximity mines they walk over.
    std::function<void(const std::function<void(const Vec3& position)>& visit)> backgroundGuards;
    // bossGetStageNum.
    std::function<LevelId()> stage;
    // SurroundWithExplosions and countdownTimerSetVisible(2, FALSE), for a
    // plastique charge going off.
    std::function<void(int ticks)> surroundBond;
    // objectivestatusCheckDeposit.
    std::function<void(int weapon, int room)> checkDeposit;
    // The player the tick runs for (get_cur_playernum).
    std::function<int()> player;
    // lvlGetControlsLockedFlag.
    std::function<bool()> controlsLocked;
};

// The flight of thrown, dropped and fired objects, the fuses and mines of
// the weapons that go off, and what makes an object fall (propobj.c).
class Projectiles {
  public:
    // PROJECTILE_LIFETIME_FRAMES, GRENADE_SMOKE_FRAMES and PROXIMITY_MINE_TRIGGER_DISTANCE.
    static constexpr uint32_t kLifetimeTicks = 2400;
    static constexpr uint32_t kGrenadeSmokeTicks = 301;
    static constexpr float kProximityRangeSquared = 62500.0f;
    static constexpr int kMaxProximityMines = 30;

    void Attach(Shots* shots, ProjectileHooks hooks) {
        shots_ = shots;
        hooks_ = std::move(hooks);
    }
    void SetClock(const TickClock& clock) {
        clock_ = clock;
    }
    // init_load_objpos_table's share: no mines armed, no remote trigger.
    void Reset();

    // The same for a door that fell away (doorFinishOpen hands it a projectile),
    // and the hand-off. `simOwner` is objTick's isSimOwner: whether this is the
    // pass that moves the object, of the several a frame has in a multiplayer
    // game.
    TickOp TickDoor(int prop, bool simOwner = true);
    void StartDoorFall(PlacedDoor& door);

    // objTick's first half for an object that is not removed: a flying object
    // moves one tick. `moved` is whether it did (objMovedThisFrame); the object's
    // footprint, rooms, shading and the mines around it are updated here.
    TickOp TickFlight(int prop, bool simOwner = true);

    // chrobjWeaponTick.
    void WeaponTick(int prop);
    // objDropRecursively.
    void DropRecursively(int prop);
    // propExplode.
    void Explode(int prop, int type);

    // objFall and objBounce (view-space direction), and
    // objDestroySupportedObjects.
    void Fall(PlacedObject& object, int player);
    void Bounce(PlacedObject& object, const Vec3& viewDirection);
    void DestroySupported(int tableProp, int player);

    // objApplyDamage with a few points: what a fall costs the object.
    void LandingDamage(PlacedObject& object);
    // The end of propsTick: check_guard_detonate_proxmine, then the remote mine
    // trigger is cleared.
    void EndTick();

    // trigger_remote_mine_detonation.
    void TriggerRemoteMines() {
        remoteMineTrigger_ |= 1U << static_cast<unsigned>(Player());
    }
    // add_obj_to_temp_proxmine_table, remove_obj_from_temp_proxmine_table and
    // detonate_proxmine_In_range.
    void AddProximityMine(int prop);
    void RemoveProximityMine(int prop);
    void DetonateProximityMines(const Vec3& position);

    // gunInitProjectileObject: the object `prop` starts flying from `position`
    // on tile `stan` with `matrix` and `velocity`.
    void InitObject(int prop, const Vec3& position, int stan, Mtxf matrix, const Vec3& velocity, const Mtxf& spin,
                    int ownerProp);
    // gunInitProjectileFromPlayer: the safest place to start from, then
    // InitObject.
    void InitFromPlayer(int prop, const Vec3& target, const Mtxf& matrix, const Vec3& velocity, const Mtxf& spin,
                        int playerProp, float standHeight, const Vec3& viewerPosition, int playerRoom);

    // sub_GAME_7F042EB4: moves the object towards `target` (handles_projectile_motion
    // for one in flight, objTryMovePropWithCollision otherwise).
    int Move(PlacedObject& object, const Vec3& target, Vec3* hitPos, Vec3* hitNormal, bool canEmbed,
             bool onlyIfPathClear);

    // objUpdateThrowKnifeSound.
    void UpdateKnifeSound(PlacedObject& object);
    // sub_GAME_7F0439B8: sets the object on the surface it hit.
    void StickToSurface(PlacedObject& object, const Vec3& point, int stan, const Vec3& normal);
    // objEmbed.
    bool Embed(int prop, int parentProp, const Model::ModelInstance* model, int32_t node);
    // objSettle.
    void Settle(PlacedObject& object, const Vec3& previousXAxis);

  private:
    int Player() const {
        return hooks_.player ? hooks_.player() : 0;
    }
    // handles_projectile_motion and objTryMovePropWithCollision.
    int MoveProjectile(PlacedObject& object, const Vec3& target, Vec3* hitPos, Vec3* hitNormal, bool canEmbed,
                       bool onlyIfPathClear);
    int MoveObject(PlacedObject& object, const Vec3& target, Vec3* hitPos, Vec3* hitNormal);
    // bgFindRoomsAlongSegment: the rooms between, ended by 0xFF, and the list of
    // those traversed.
    void FindRooms(const Vec3& from, const Vec3& to, const std::array<uint8_t, 8>& fromRooms,
                   std::array<uint8_t, 8>* finalRooms, std::vector<int>* traversed, int maxTraversed) const;
    // objTick's first half for the record of an object or the loan of a door's.
    TickOp TickBody(PlacedObject& object, bool simOwner);
    // The tick of an object in the air and of one that has come to rest.
    TickOp TickAirborne(PlacedObject& object, Projectile& projectile, Vec3* current, bool* moved);
    void TickLanded(PlacedObject& object, Projectile& projectile, Vec3* current, bool* moved);
    // mtxLoadRandomRotation and sub_GAME_7F057C14.
    Mtxf RandomRotation();
    float RandomFraction();
    // objChangeShading/setupUpdateObjectRoomPosition after the object moved.
    void AfterMove(PlacedObject& object);

    Shots* shots_ = nullptr;
    ProjectileHooks hooks_;
    TickClock clock_;
    // g_RemoteMineOwnerTriggerFlag and proxy_mine_table.
    uint32_t remoteMineTrigger_ = 0;
    std::array<int, kMaxProximityMines> proximityMines_{};
    // D_80030B0C and the rest of what projectileFindCollidingProp leaves behind.
    ProjectileContact contact_;
};

} // namespace Frigate::Level
