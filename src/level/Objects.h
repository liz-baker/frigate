#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "level/Matrix.h"
#include "level/ModelHandle.h"
#include "level/PropList.h"
#include "level/PropModels.h"
#include "level/StageSetup.h"
#include "level/StanProps.h"

namespace Frigate::Level {

class BgLevel;
class Stan;
class Inventory;
class Lang;
class PlayerWeapons;
struct PropDef;
class WeaponTable;

// The bits of ObjectRecord::flags the placement and collision code tests
// (PROPFLAG_*, by their meaning in the setup editor).
namespace ObjectFlag {
// Sits in the air, upside down.
constexpr uint32_t InAirUpsideDown = 0x4;
// Sits in the air.
constexpr uint32_t InAir = 0x8;
// Sits in the air, rotated 90 degrees and upside down (the decomp's
// PROPFLAG_ONSCREEN).
constexpr uint32_t InAirRotated = 0x2;
// Scales to its bound pad's box on every axis (0x10) or on one (0x20, 0x40,
// 0x80 for x, y, z).
constexpr uint32_t ScaleToPad = 0x10;
constexpr uint32_t ScaleToPadX = 0x20;
constexpr uint32_t ScaleToPadY = 0x40;
constexpr uint32_t ScaleToPadZ = 0x80;
// Collides even when its model would not.
constexpr uint32_t ForceCollisions = 0x100;
// Ignores the stan tile's colour.
constexpr uint32_t Illuminated = 0x400;
constexpr uint32_t FreeStandingGlass = 0x800;
// Sits exactly at its pad's position.
constexpr uint32_t AbsolutePosition = 0x1000;
constexpr uint32_t AssignedToChr = 0x4000;
// Embedded in the previous object.
constexpr uint32_t InsideAnotherObject = 0x8000;
constexpr uint32_t Invincible = 0x20000;
// Allow Pickup: gates automatic pickup for a type (a hat) that otherwise needs
// the interact button (objTickPlayer's PROPFLAG_00040000).
constexpr uint32_t AllowPickup = 0x40000;
// Collect Object by Interaction Button Only: skips the automatic pickup test.
constexpr uint32_t CollectByInteractOnly = 0x80000;
// Item Not Collectable: a collectable/key/hat/ammo/armour prop that currently
// can't be picked up.
constexpr uint32_t Uncollectable = 0x100000;
constexpr uint32_t CannotActivate = 0x2000000;
// AI Sees Through Door/Object (PROPFLAG_04000000): the same bit
// propDoorGetCdTypes tests for a door.
constexpr uint32_t AiSeesThrough = 0x4000000;
// Open Away From Player: a two-way door picks its swing side by which side
// of the pad first opens it.
constexpr uint32_t DoorTwoWay = 0x8000000;
// A glass object has a portal that opens when it shatters.
constexpr uint32_t GlassHasPortal = 0x10000000;
// Area Behind Door Invisible: culls the room across the door's portal while
// closed.
constexpr uint32_t CullBehindDoor = 0x10000000;
// Left-Handed weapon: a collectable's weaponLinkedType names the weapon it
// pairs with in the right hand instead of the left (bondinvAddWeaponByProp).
constexpr uint32_t WeaponLeftHanded = 0x10000000;
// Open Backwards: swings/slides the door toward the pad's look direction
// instead of away from it.
constexpr uint32_t DoorOpenToFront = 0x20000000;
// Conceal Weapon (PROPFLAG_CONCEAL_GUN): a weapon a character carries but
// does not hold.
constexpr uint32_t ConcealGun = 0x20000000;
// PROPFLAG_IS_DOUBLE: a character's weapon that pairs with the one in its
// other hand.
constexpr uint32_t WeaponIsDouble = 0x80000000;
// Area Behind Door Visible: keeps the door's portal open even while closed
// (only the door's own collision blocks the doorway).
constexpr uint32_t NoPortalClose = 0x40000000;
// No Ammo on pickup: a collectable weapon grants no ammo for its type.
constexpr uint32_t WeaponNoAmmo = 0x40000000;
// Open By Default: the door starts fully open.
constexpr uint32_t OpenByDefault = 0x80000000;
// Render Post-BG: a monitor's screen draws in the post-background pass
// (PropFlag::RenderPostBg) instead of the usual one.
constexpr uint32_t MonitorRenderPostBg = 0x40000000;
} // namespace ObjectFlag

// The bits of ObjectRecord::flags2 the placement code tests.
namespace ObjectFlag2 {
// PROPFLAG2_00080000: a held weapon with it set is not drawn.
constexpr uint32_t Unnamed0x80000 = 0x80000;
// Places the object at its pad's position without walking the stan tiles to it
// (an autogun's "activate drone gun").
constexpr uint32_t PlaceAtPad = 0x1;
// The object's rooms are those of its stan tile alone.
constexpr uint32_t TileRoomsOnly = 0x20000;
// Character AI Cannot Operate: propDoorGetCdTypes reports the door as
// CdType::DoorsLockedToAi.
constexpr uint32_t AiCannotOperate = 0x20000000;
// Close first door before opening second: a linked door's sibling closes
// instead of opening in step with it.
constexpr uint32_t DoorCloseFirst = 0x40000000;
// Watch Magnet Attract through Walls/Objects: skips the pickup test's
// line-of-sight check against closed doors, regardless of whether the
// magnet's own widened radius (not yet ported, see #183) applies.
constexpr uint32_t MagnetThroughWalls = 0x1000;
} // namespace ObjectFlag2

// PROPSTATE_*.
namespace ObjectState {
constexpr uint8_t Respawn = 0x04;
constexpr uint8_t ExtCollisionBlock = 0x08;
constexpr uint8_t State20 = 0x20;
} // namespace ObjectState

// ObjectRecord::runtime_bitflags.
namespace ObjectRuntime {
// Attached to another object (RUNTIMEBITFLAG_HASOWNER).
constexpr uint32_t HasOwner = 0x00080000;
// Attached to a node of another object's model (RUNTIMEBITFLAG_EMBEDDED).
constexpr uint32_t Embedded = 0x00000040;
// A weapon that stays out of sight (RUNTIMEBITFLAG_00000800).
constexpr uint32_t Unnamed0x800 = 0x00000800;
// RUNTIMEBITFLAG_HASPROJECTILE: the object owns a live projectile record.
constexpr uint32_t HasProjectile = 0x00000080;
// Rests on the object beneath it (RUNTIMEBITFLAG_00008000).
constexpr uint32_t RestingOnObject = 0x8000;
// RUNTIMEBITFLAG_TAGGED: a tag record names the object, so a pickup of it is
// kept in the inventory rather than freed.
constexpr uint32_t Tagged = 0x00000010;
// RUNTIMEBITFLAG_DESTROYED: a rename record targets the object, or a
// character's weapon was dropped (its pickup adds no inventory entry of its
// own weapon).
constexpr uint32_t Destroyed = 0x00000400;
} // namespace ObjectRuntime

// collision_data: an object's footprint in the world.
struct CollisionBlock {
    int edges = 0;
    std::array<Vec2, 8> polygon{};
    float top = 0;
    float bottom = 0;
};

// The part of an ObjectRecord the stage builds and the collision tests read.
struct PlacedObject {
    // The propdef record it was made from, as an index into
    // StageSetup::PropDefs().
    size_t def = 0;
    // The record's type byte (PROPDEF_*).
    uint8_t type = 0;
    // obj: the object's model, an index into the model table.
    int model = 0;
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    uint8_t state = 0;
    uint32_t runtimeFlags = 0;
    // model->scale.
    float scale = 0.1f;
    Mtxf mtx{};
    Vec3 runtimePosition{};
    // The object's slot in the prop list.
    int prop = -1;
    // ptr_allocated_collisiondata_block: present for an object with
    // ObjectFlag::ForceCollisions.
    std::optional<CollisionBlock> collision;
    // A tinted glass object's portal (portalnum), -1 for none.
    int portal = -1;
    // WeaponObjRecord::weaponnum and ::LinkedWeaponType, for the weapons a
    // character holds.
    int32_t weaponNum = 0;
    int32_t weaponLinkedType = -1;
    // The model a held weapon, a hat or an embedded object poses with: its
    // holder's node matrix is its base (Model::ModelInstance::AttachTo). Null
    // for an object placed in the world.
    std::shared_ptr<ModelHandle> instance;
    // obj->embedment->matrix, for an object with ObjectRuntime::Embedded: its
    // placement relative to the node of its parent's model it is attached to.
    Mtxf embedment{};
    // The switch-array slot of the parent's model it is attached to.
    size_t embedSlot = 0;
};

// What weaponAssignToHome and setupHat need of the character a record names
// (chrFindByLiteralId, which must have a prop and a model).
struct ChrHost {
    int prop = -1;
    std::shared_ptr<ModelHandle> model;
    // weapons_held (indexed by hand) and handle_positiondata_hat, which
    // equipping writes.
    std::array<int, 2>* weaponsHeld = nullptr;
    int* hat = nullptr;
};

// propPickupByPlayer's result.
enum class PickupOutcome {
    None,
    // TICKOP_FREE: the pickup is complete: deregister and free the prop.
    Collected,
    // TICKOP_GIVETOPLAYER: the prop is kept as an inventory entry of its own
    // (bondinvAddPropToInv), taken out of the world.
    Given,
};

// The stage's objects and the prop list they live in: the part of
// proplvreset2 that creates plain objects (domakedefaultobj).
class Objects final : public StanProps {
  public:
    // init_load_objpos_table, for a stage with `roomCount` rooms.
    void Reset(int roomCount, Stan* stan, const BgLevel* level, std::shared_ptr<const PropModels> models);
    // Where the models of held weapons and hats load from; without them those
    // objects are still created, unposed.
    void SetModels(ModelFactory* models) {
        models_factory_ = models;
    }

    // Whether Create serves records of `type`: the ones domakedefaultobj
    // places directly (PROP, ALARM, MAGAZINE, GAS_RELEASING, UNK41, GLASS,
    // TINTED_GLASS, SAFE), plus the pickup types whose own setup functions
    // (weaponAssignToHome, setupKey, setupHat) fall through to
    // domakedefaultobj once assigned-to-character placement is ruled out
    // (COLLECTABLE, KEY, HAT, AMMO, ARMOUR).
    static bool Serves(uint8_t type);
    // Creates the object of propdef record `def` as proplvreset2 does for the
    // types Serves names. False for any other type, or when the object could
    // not be placed.
    bool Create(const StageSetup& setup, size_t def);
    // The pass of proplvreset2 after every object exists: each object flagged
    // as inside another is attached to the record its pad number names, an
    // offset from its own.
    void LinkEmbedded(const StageSetup& setup);
    // The object created from propdef record `def`, or null when there is none.
    const PlacedObject* ForDef(size_t def) const;
    // The object with prop `prop`, or null.
    PlacedObject* ForProp(int prop);
    const PlacedObject* ForProp(int prop) const;

    const std::vector<PlacedObject>& All() const {
        return objects_;
    }
    // PitemZ_entries[index]; null for an index outside the table or a stage
    // with no model table loaded.
    const PropModel* Model(int index) const {
        return models_ ? models_->Model(index) : nullptr;
    }
    PropList& Props() {
        return props_;
    }
    const PropList& Props() const {
        return props_;
    }

    // chraiGetCollisionBounds for an object's prop (sub_GAME_7F04F244): the
    // footprint, or null when it has none.
    const CollisionBlock* Collision(int prop) const;

    // The player's own prop is a viewer prop: `footprint` fills its footprint
    // (bondviewGetPropHeightRelatedValues) and returns false when it has none
    // right now.
    void SetViewerFootprint(std::function<bool(PropCollider*)> footprint) {
        viewerFootprint_ = std::move(footprint);
    }

    // Doors (#158) register these once they exist, so Colliders can serve
    // PropType::Door props from the same shared PropList without Objects
    // depending on the Doors class: whether a door prop passes propIsOfCdType
    // for a set of CdType::* bits, and its footprint (chraiGetCollisionBounds),
    // null when it has none right now.
    void SetDoorCollision(std::function<bool(int, int)> passes, std::function<const CollisionBlock*(int)> collision) {
        doorPasses_ = std::move(passes);
        doorCollision_ = std::move(collision);
    }

    // The stage's characters register this so Colliders can serve
    // PropType::Chr props: whether the chr prop passes propIsOfCdType for a
    // set of CdType::* bits and has a footprint (chrUpdateCollisionBounds),
    // which it then fills.
    void SetChrCollision(std::function<bool(int, int, PropCollider*)> collider) {
        chrCollider_ = std::move(collider);
    }

    // chrFindByLiteralId, for the objects assigned to a character: the chr
    // numbered `chrnum`, with a null prop when there is none.
    void SetChrLookup(std::function<ChrHost(int)> chrHost) {
        chrHost_ = std::move(chrHost);
    }

    void Colliders(const int* rooms, int cdTypes, std::vector<PropCollider>* out) const override;

    // objTickPlayer's proximity/eligibility test for a pickup prop -- a
    // COLLECTABLE, KEY, HAT, AMMO or ARMOUR object (false for any other
    // type, including PropDefType::Magazine's own pickup test, not yet
    // ported -- see https://github.com/liz-baker/frigate/issues/185), minus
    // the parts https://github.com/liz-baker/frigate/issues/183 (magnet
    // attract's widened radius, the look-down gate, thrown-weapon
    // exclusions) and https://github.com/liz-baker/frigate/issues/184 (safe
    // locking) track. `setup` resolves `object`'s propdef record for its
    // type-specific fields; `playerPosition` and `playerStan` are the
    // player's own prop position and tile, and `playerArmour` its current
    // body armour (currentPlayerGetArmor).
    // `playerPitchRadians` is the player's look pitch (vv_verta in radians).
    bool TestPickup(const StageSetup& setup, const PlacedObject& object, const Vec3& playerPosition, int playerStan,
                    float playerArmour, const Inventory& inventory, const PlayerWeapons& weapons,
                    const WeaponTable& weaponTable, float playerPitchRadians = 0.0f) const;
    // propPickupByPlayer for a KEY/AMMO/ARMOUR/COLLECTABLE object, or a HAT
    // or any other type, which the decomp's default case keeps as an inventory
    // entry (PickupOutcome::Given) whatever the type -- the single ammo crate
    // (Magazine) is https://github.com/liz-baker/frigate/issues/185.
    // `armour` is the player's current body armour
    // (currentPlayerGetArmor/bondviewAddCurrentPlayerArmor), updated in
    // place; `soloAmmoMultiplier` is g_SoloAmmoMultiplier. The messages the
    // pickup shows (hudmsgBottomShow) are appended to `messages`, worded from
    // `lang`; without both there are none.
    PickupOutcome Collect(const StageSetup& setup, const PlacedObject& object, const WeaponTable& weaponTable,
                          PlayerWeapons* weapons, Inventory* inventory, float* armour, float soloAmmoMultiplier = 1.0f,
                          const Lang* lang = nullptr, std::vector<std::string>* messages = nullptr) const;

    // propExecuteTickOperation for a pickup's outcome: TICKOP_FREE takes the
    // prop out of the world and frees it, TICKOP_GIVETOPLAYER hands it to the
    // player's prop, out of the world too.
    void Free(int prop);
    void GiveToPlayer(int prop, int playerProp);

  private:
    // propPickupByPlayer's tail: a freed pickup of a tagged object is kept.
    PickupOutcome Finish(const PlacedObject& object, bool freed) const;
    // bondinvAddWeaponByProp: whether the pickup added an inventory entry.
    bool AddWeaponByProp(Inventory* inventory, const PlacedObject& object, const PropDef& def) const;
    // Adds `object` to the stage's objects and its prop's link to it.
    PlacedObject& Store(const PlacedObject& object);
    // weaponAssignToHome's assigned-to-character branch and
    // sub_GAME_7F051F30 (chrEquipWeapon): the weapon of propdef record `def`,
    // held by `host`.
    bool AssignWeapon(const PropDef& def, size_t defIndex, const PropModel& model, const ChrHost& host);
    // setupHat and hatApplyToChr.
    bool AssignHat(const PropDef& def, size_t defIndex, const PropModel& model, const ChrHost& host);
    // setupSingleMonitor's embedded branch: a monitor on a node of an earlier
    // object's model.
    bool EmbedMonitor(const StageSetup& setup, const PropDef& def, size_t defIndex, const PropModel& model);
    // The instance of `model` for `object`, scaled as its record scales it.
    // Null without a resource source.
    std::shared_ptr<ModelHandle> Instantiate(const PropModel& model, float scale) const;
    // objInit: the prop and record for `object`, whose model is `model`. -1
    // when the prop pool is used up.
    int Init(PlacedObject* object, const PropModel& model);
    const ModelBounds* Bounds(const PlacedObject& object) const;
    // sub_GAME_7F04088C (moveToPad): places the object at `position` (its
    // anchor `anchor`) and, for the in-air-rotated variant, sub_GAME_7F040BA0.
    void MoveToPad(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan, const Vec3& anchor);
    void MoveToPadRotated(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan, const Vec3& anchor);
    // objChangeShading.
    void SetPosition(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan);
    // chrobjCollisionRelated.
    void UpdateCollision(PlacedObject* object);
    // setupUpdateObjectRoomPosition.
    void UpdateRooms(PlacedObject* object);
    // sub_GAME_7F03FAB0: the object whose footprint holds `position`, among
    // those standing in `room`.
    const PlacedObject* ObjectBeneath(const Vec3& position, int room) const;
    // walkTilesBetweenPoints_NoCallback from a tile that may be missing.
    bool Walk(int* tile, const Vec3& from, const Vec3& to) const;

    std::vector<PlacedObject> objects_;
    // The index in objects_ of the object each propdef record made, -1 for none.
    std::vector<int> byDef_;
    std::function<bool(PropCollider*)> viewerFootprint_;
    std::function<bool(int, int)> doorPasses_;
    std::function<bool(int, int, PropCollider*)> chrCollider_;
    std::function<ChrHost(int)> chrHost_;
    ModelFactory* models_factory_ = nullptr;
    std::function<const CollisionBlock*(int)> doorCollision_;
    PropList props_;
    Stan* stan_ = nullptr;
    const BgLevel* level_ = nullptr;
    std::shared_ptr<const PropModels> models_;
};

// chrpropSumMatrixPosX/Y/Z and chrpropSumMatrixNegX/Y/Z: the model box's
// extent along one axis of the world after `matrix`. The "Pos" sums take the
// low bound of each model axis whose matrix entry is not negative, and the
// "Neg" sums the low bound of each whose entry is not positive.
float SumMatrixPos(const ModelBounds& bounds, const Mtxf& matrix, int axis);
float SumMatrixNeg(const ModelBounds& bounds, const Mtxf& matrix, int axis);

// sub_GAME_7F03ECC0: the footprint of a model box under `matrix`, as a convex
// polygon of up to eight points translated to the matrix's position.
void BuildCollisionPolygon(const ModelBounds& bounds, const Mtxf& matrix, CollisionBlock* out);

// sub_GAME_7F001BD4 (padGetCentre): the middle of a bound pad's box.
Vec3 BoundPadCentre(const BoundPad& pad);

// chrpropTestPointInPolygon: whether a world position is inside a convex
// polygon.
bool PointInPolygon(const Vec3& point, const std::array<Vec2, 8>& polygon, int edges);

// g_SoloAmmoMultiplier, as set for `difficulty` (0 to 2, matching
// StageSetup::LoadsObject's) by the difficulty-select screen's
// DEFAULT_AGENT_SOLO_AMMO_MULTIPLIER/DEFAULT_SECRET_AGENT_SOLO_AMMO_MULTIPLIER/
// DEFAULT_00_AGENT_SOLO_AMMO_MULTIPLIER. That screen isn't ported yet, so
// callers without a real difficulty selection should pass 1.0 (00 Agent's
// value) explicitly rather than call this.
float SoloAmmoMultiplier(int difficulty);

} // namespace Frigate::Level
