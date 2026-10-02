#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "level/Matrix.h"
#include "level/MonitorScreen.h"
#include "level/Shading.h"
#include "level/ModelHandle.h"
#include "level/Projectile.h"
#include "level/PropList.h"
#include "level/PropModels.h"
#include "level/StageSetup.h"
#include "level/StanProps.h"
#include "level/Tank.h"

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
// Fall to Ground (PROPFLAG_RENDERPOSTBG): the object falls when what holds it
// up is destroyed.
constexpr uint32_t Fall = 0x1;
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
// PROPFLAG_AIUNDROPPABLE: a character's weapon that is not dropped when it
// dies or is hit.
constexpr uint32_t AiUndroppable = 0x2000;
constexpr uint32_t AssignedToChr = 0x4000;
// Embedded in the previous object.
constexpr uint32_t InsideAnotherObject = 0x8000;
constexpr uint32_t ForceMortal = 0x10000;
constexpr uint32_t Invincible = 0x20000;
// Bounce and Destroy If Shot (PROPFLAG_00200000).
constexpr uint32_t BounceIfShot = 0x200000;
// PROPFLAG_00400000: a pickup it is set on does not bounce when shot.
constexpr uint32_t NoBounceIfShot = 0x400000;
// PROPFLAG_00800000 and PROPFLAG_01000000: whether a barehanded hit can damage
// a pickup (set) or a plain object (clear).
constexpr uint32_t HittableCollectable = 0x800000;
constexpr uint32_t UnhittableObject = 0x1000000;
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
// Disable security camera (PROPFLAG_CCTV_DISABLED) and drone gun
// (PROPFLAG_IS_DRONE_GUN).
constexpr uint32_t CctvDisabled = 0x10000000;
constexpr uint32_t DroneGun = 0x10000000;
// Left-Handed weapon: a collectable's weaponLinkedType names the weapon it
// pairs with in the right hand instead of the left (bondinvAddWeaponByProp).
constexpr uint32_t WeaponLeftHanded = 0x10000000;
// Open Backwards: swings/slides the door toward the pad's look direction
// instead of away from it.
constexpr uint32_t DoorOpenToFront = 0x20000000;
// PROPFLAG_INMOTION: a vehicle that has not yet worked out its heading.
constexpr uint32_t InMotion = 0x20000000;
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
// Open By Default: the door starts fully open. The same bit is
// PROPFLAG_DOOR_KEEPOPEN at run time: a door that stays open until something
// activates it.
constexpr uint32_t OpenByDefault = 0x80000000;
constexpr uint32_t DoorKeepOpen = 0x80000000;
// Render Post-BG: a monitor's screen draws in the post-background pass
// (PropFlag::RenderPostBg) instead of the usual one.
constexpr uint32_t MonitorRenderPostBg = 0x40000000;
} // namespace ObjectFlag

// The bits of ObjectRecord::flags2 the placement code tests.
namespace ObjectFlag2 {
// PROPFLAG2_00000002: lightweight; the object moves when shot once it is
// destroyed.
constexpr uint32_t Lightweight = 0x2;
// PROPFLAG2_NOFALL: immobile.
constexpr uint32_t NoFall = 0x100;
// PROPFLAG2_FALLWITHOUTROTATION: used with mines.
constexpr uint32_t FallWithoutRotation = 0x200;
// PROPFLAG2_THROWTHROUGH: rockets, mines and grenades pass through.
constexpr uint32_t ThrowThrough = 0x02000000;
// PROPFLAG2_00002000: the object is removed when destroyed.
constexpr uint32_t RemoveWhenDestroyed = 0x2000;
// PROPFLAG2_00004000: immune to gunfire.
constexpr uint32_t ImmuneToGunfire = 0x4000;
// PROPFLAG2_SHOOTTHROUGH: a shot passes through.
constexpr uint32_t ShootThrough = 0x8000;
// PROPFLAG2_00080000: invincible except to explosions.
constexpr uint32_t InvincibleExceptExplosions = 0x80000;
// PROPFLAG2_00100000: bulletproof glass.
constexpr uint32_t Bulletproof = 0x100000;
// PROPFLAG2_00200000: immune to explosions.
constexpr uint32_t ImmuneToExplosions = 0x200000;
// PROPFLAG2_00080000: a held weapon with it set is not drawn.
constexpr uint32_t Unnamed0x80000 = 0x80000;
// Always Visible (in front): drawn without the depth test, in the blended
// pass.
constexpr uint32_t AlwaysVisible = 0x10000;
// PROPFLAG2_04000000: the decomp does not name it; an object with it set is
// drawn under the whole view's scissor instead of its rooms'.
constexpr uint32_t Unnamed0x4000000 = 0x4000000;
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
// Interaction Text Not Shown for Doors: a locked door shows no message.
constexpr uint32_t DoorNoInteractText = 0x4;
// PROPFLAG2_00000008 ("Don't Load in Multiplayer" in the setup editor): at run
// time a door the player tried and failed to open has it set.
constexpr uint32_t DoorTriedLocked = 0x8;
// A safe item's record (objCanPickupFromSafe), and the door it is locked
// behind.
constexpr uint32_t LinkedToSafe = 0x400;
// Don't activate if a stan gap lies between Bond and the object.
constexpr uint32_t InteractCheckLos = 0x800;
// One-Way Lock (Front) and (Back): a keyed door the player opens only from the
// side the key is not needed on.
constexpr uint32_t OneWayLockFront = 0x08000000;
constexpr uint32_t OneWayLockBack = 0x10000000;
// A door is also tested for interaction against the pad's side axis, and
// not only its up axis (door7F0555F8's second pass).
constexpr uint32_t DoorAltCoordSystem = 0x80000000;
} // namespace ObjectFlag2

// PROPSTATE_*.
namespace ObjectState {
// PROPSTATE_DAMAGED and PROPSTATE_2: a mark is on the object to draw in the
// opaque and the blended pass.
constexpr uint8_t Marked = 0x01;
constexpr uint8_t MarkedBlended = 0x02;
constexpr uint8_t Respawn = 0x04;
constexpr uint8_t ExtCollisionBlock = 0x08;
constexpr uint8_t State10 = 0x10;
constexpr uint8_t State20 = 0x20;
// PROPSTATE_ACTIVATED: a gadget was used on the object.
constexpr uint8_t Activated = 0x40;
// PROPSTATE_DESTROYED.
constexpr uint8_t Destroyed = 0x80;
} // namespace ObjectState

// ObjectRecord::runtime_bitflags.
namespace ObjectRuntime {
// RUNTIMEBITFLAG_00000001: a switch record's object, which also activates
// the door the record names.
constexpr uint32_t LinkedToSwitch = 0x00000001;
// RUNTIMEBITFLAG_00000002: the decomp does not name it; an object carrying it
// can be used, like a tagged one.
constexpr uint32_t Unnamed0x2 = 0x00000002;
// RUNTIMEBITFLAG_OWNER: the player a kill by the object is credited to.
constexpr uint32_t OwnerMask = 0x60000;
constexpr uint32_t OwnerShift = 0x11;
// RUNTIMEBITFLAG_00001000: the object is waiting to respawn, and cannot be shot.
constexpr uint32_t Regenerating = 0x00001000;
// RUNTIMEBITFLAG_BEENOPENED: a door that has started to open at least once.
constexpr uint32_t BeenOpened = 0x00000200;
// RUNTIMEBITFLAG_PADLOCKEDDOOR: a door a lock-door record names.
constexpr uint32_t PadlockedDoor = 0x00002000;
// RUNTIMEBITFLAG_REMOVE: the object is removed on its next tick.
constexpr uint32_t Remove = 0x00000004;
// RUNTIMEBITFLAG_ACTIVATED: the player used the object or door.
constexpr uint32_t Activated = 0x00004000;
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
// RUNTIMEBITFLAG_ISRETICK: the object was ticked once ahead of its projectile
// tick, which runs at the end of the frame's walk.
constexpr uint32_t IsRetick = 0x00000008;
// RUNTIMEBITFLAG_THROWING_KNIFE_RELATED: a thrown knife that has not hit yet.
constexpr uint32_t ThrowingKnife = 0x00000020;
// RUNTIMEBITFLAG_00000100: the object takes a few points of damage on its next
// tick (it landed after falling).
constexpr uint32_t DamageOnLand = 0x00000100;
// RUNTIMEBITFLAG_00010000: the object fell because it was destroyed; its
// landing does not hurt it.
constexpr uint32_t FellDestroyed = 0x00010000;
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

// The AI list state and motion of a vehicle or aircraft (VehichleRecord and
// AircraftRecord).
struct ObjectAi {
    const AiList* list = nullptr;
    uint16_t offset = 0;
    int16_t returnList = -1;
    // The aircraft's pad (AI_SetMyPadPreset sets it).
    int pad = 0;
    // speed, speedaim and speedtime60; a negative time is no change under way.
    float speed = 0.0f;
    float speedAim = 0.0f;
    float speedTime60 = -1.0f;
    // The patrol path being followed (an index into the setup's paths, -1 for
    // none) and the waypoint of it being driven to.
    int path = -1;
    int nextStep = 0;
    // A vehicle's heading (roty) and its turn speed (turnrot60).
    float yaw = 0.0f;
    float turnRate = 0.0f;
    // An aircraft's rotor: rotaryspeed, rotaryspeedaim and rotaryspeedtime.
    float rotorSpeed = 0.0f;
    float rotorSpeedAim = 0.0f;
    float rotorSpeedTime = -1.0f;
};

// PlacedObject::def of an object no propdef record made (one the AI gave a
// character).
constexpr size_t kNoDef = static_cast<size_t>(-1);

// The part of an ObjectRecord the stage builds and the collision tests read.
struct PlacedObject {
    // The propdef record it was made from, as an index into
    // StageSetup::PropDefs(), or kNoDef.
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
    // The screens of a monitor or multi-monitor.
    MonitorScreens monitor;
    // WeaponObjRecord::weaponnum and ::LinkedWeaponType, for the weapons a
    // character holds.
    int32_t weaponNum = 0;
    int32_t weaponLinkedType = -1;
    // WeaponObjRecord::dualweapon: the propdef record of the weapon it pairs
    // with while neither is picked up, -1 for none. The partner may not have
    // been created (on another difficulty), which still counts as a pairing.
    int64_t dualWeapon = -1;
    // The same pairing for a weapon no propdef record made (a character's, or
    // one it dropped): the partner's index among the stage's objects, -1 for none.
    int64_t dualObject = -1;
    // The model a held weapon, a hat or an embedded object poses with: its
    // holder's node matrix is its base (Model::ModelInstance::AttachTo). Null
    // for an object placed in the world.
    std::shared_ptr<ModelHandle> instance;
    // obj->embedment->matrix, for an object with ObjectRuntime::Embedded: its
    // placement relative to the node of its parent's model it is attached to.
    Mtxf embedment{};
    // The switch-array slot of the parent's model it is attached to.
    size_t embedSlot = 0;
    // shadecol and nextcol: the floor-tile shading the object is drawn with and
    // the shading it eases towards.
    ShadeColour shadeColour{};
    ShadeColour nextColour{};
    // The AI state of a vehicle or aircraft.
    ObjectAi ai;
    // The runtime fields of a tank.
    TankState tank;
    // damage and maxdamage: the object's health, and the damage it has taken
    // (times 250 until it is destroyed, then the shots it takes after).
    float damage = 1000.0f;
    float maxDamage = 0.0f;
    // WeaponObjRecord::timer: the ticks a thrown or planted weapon has left
    // before it goes off; -1 for none.
    int16_t weaponTimer = -1;
    // BodyArmourRecord::amount: the armour picking the object up gives, which
    // damage takes from.
    float armourAmount = 0.0f;
    // AmmoCrateRecord::ammoType (a Magazine) and the slots of a crate of
    // several kinds of ammo (an Ammo object).
    int32_t ammoType = 0;
    std::array<PropDef::AmmoSlot, PropDef::kAmmoSlotCount> ammoSlots{};
    // A tinted glass object's clear and opaque distances, the opacity it has
    // up close (unk90) and the opacity worked out for this tick
    // (calculatedopacity).
    float tintDistance = 0.0f;
    float cullDistance = 0.0f;
    float tintNearOpacity = 0.0f;
    int calculatedOpacity = 0xFF;
    // The switch slots of the object's model that are hidden (a camera's
    // broken glass).
    std::vector<uint8_t> hiddenSwitches;
    // The slot in the projectile pool of the record an item that was dropped,
    // thrown or fired carries while ObjectRuntime::HasProjectile is set, and of
    // the projectile an embedded object carries (embedment->projectile); -1 for
    // none.
    int projectile = -1;
    int embedmentProjectile = -1;
    // The embedment record is allocated from the pool (embedmentAllocate).
    bool hasEmbedment = false;
    // The index in the parent's model of the node an embedded object is
    // attached to (attachedto_objinst), when it is not one of the model's
    // switch nodes (embedSlot is then unused).
    int32_t embedNode = -1;
    // The index of the vertex block the object's deformed collision node draws
    // with (vtxslot), -1 while it draws the model's own.
    int deformBlock = -1;
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

// What a door's blocked-by test reads of a character or the player
// (chrpropGetCollisionBounds and sub_GAME_7F03CFE8): the collision radius, the
// height and the lower bound above the ground, and the ground itself.
struct BodyBounds {
    float radius = 0;
    float height = 0;
    float above = 0;
    float ground = 0;
    // The character's hidden bits, null for the player.
    uint32_t* hidden = nullptr;
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
    PlacedObject* ForDef(size_t def) {
        return const_cast<PlacedObject*>(std::as_const(*this).ForDef(def));
    }
    // The geometry of a tank's model, loaded once per model; null when it has
    // none.
    const TankGeometry* TankGeometryOf(const PlacedObject& object);
    // The object with prop `prop`, or null.
    PlacedObject* ForProp(int prop);
    const PlacedObject* ForProp(int prop) const;

    const std::deque<PlacedObject>& All() const {
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
    // chraiGetCollisionBounds for any prop: false when it has no footprint.
    bool CollisionBounds(int prop, PropCollider* out) const;

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

    // The characters and the player register this for the door's blocked-by
    // test: whether the prop is a body that can block (a hidden character
    // cannot) and what its bounds are.
    void SetBodyBounds(std::function<bool(int, BodyBounds*)> bounds) {
        bodyBounds_ = std::move(bounds);
    }
    // sub_GAME_7F0448A8: whether nothing stands in the footprint of `prop`
    // (a door, after it moved): no body within it, no other object or door
    // overlapping it.
    bool FootprintClear(int prop) const;

    // Safe items (objCanPickupFromSafe): whether the object made from propdef
    // record `def` is free to pick up, which it is not while it is locked
    // behind a safe whose door is still shut. Without one every item is free.
    void SetSafeCheck(std::function<bool(size_t)> canPickup) {
        safeCheck_ = std::move(canPickup);
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
    // exclusions) tracks. `setup` resolves `object`'s propdef record for its
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
    PickupOutcome Collect(const StageSetup& setup, PlacedObject& object, const WeaponTable& weaponTable,
                          PlayerWeapons* weapons, Inventory* inventory, float* armour, float soloAmmoMultiplier = 1.0f,
                          const Lang* lang = nullptr, std::vector<std::string>* messages = nullptr);

    // chrGiveWeapon and hatCreateForChr: a new weapon of item `item` (prop model
    // `modelIndex`) or hat for the character `host` names, made with the given
    // record flags; false when the model or the prop pool is short.
    bool GiveWeapon(const ChrHost& host, int chrnum, int modelIndex, int item, uint32_t flags);
    bool GiveHat(const ChrHost& host, int modelIndex, uint32_t flags);
    // create_new_item_instance_of_model: a new weapon object of `item` made from
    // prop model `modelIndex`, out of the world until it is started flying. Its
    // prop, or -1 when the model or the prop pool is short.
    int CreateItem(int modelIndex, int item);
    // The ammo crate a destroyed multi-ammo crate drops for its slot `slot`
    // (blank_07_object, then the crate's model): its prop, or -1 when the model
    // or the prop pool is short. It is out of the world until its holder lets go.
    int CreateAmmoCrate(int modelIndex, size_t slot);
    // AI_MoveObject: moves the object onto `pad` the way its placement does
    // (sub_GAME_7F04088C, then setupUpdateObjectRoomPosition). False for an
    // object with no model bounds to place by.
    bool MoveToPadPlacement(PlacedObject* object, const Pad& pad);
    // propweaponSetDual: pairs the collectables of propdef records `leftDef`
    // and `rightDef` (weapon ids `leftWeapon` and `rightWeapon`) so picking up
    // the first adds only itself and the second completes the set. A record
    // with no object made from it is simply not there to pair.
    void SetDual(size_t leftDef, int32_t leftWeapon, size_t rightDef, int32_t rightWeapon);
    // propweaponSetDual for two weapons no propdef record made: the objects
    // for the props `leftProp` and `rightProp`.
    void SetDualProps(int leftProp, int rightProp);
    // Whether the weapons of props `a` and `b` are paired with each other.
    bool Paired(int a, int b) const;
    // objGetDestroyedLevel: 0 while the object is whole, then 1 to 3 as it takes
    // more shots once it is destroyed.
    static int DestroyedLevel(const PlacedObject& object) {
        if ((object.state & ObjectState::Destroyed) == 0) {
            return 0;
        }
        return (static_cast<int>(object.maxDamage) >> 2) + 1;
    }
    // objGetShotsTaken: how far the object is from destroyed (0 to 4 while
    // whole), then the shots it has taken since, up to 12.
    static int ShotsTaken(const PlacedObject& object) {
        if ((object.state & ObjectState::Destroyed) == 0) {
            return static_cast<int>((object.maxDamage * 3.0f) / object.damage);
        }
        return static_cast<int>(object.maxDamage + 4.0f);
    }
    // objIsHealthy.
    static bool IsHealthy(const PlacedObject& object) {
        return DestroyedLevel(object) == 0;
    }
    // objIsCollectable.
    static bool IsCollectable(uint8_t type) {
        switch (static_cast<PropDefType>(type)) {
            case PropDefType::Key:
            case PropDefType::Magazine:
            case PropDefType::Collectable:
            case PropDefType::Hat:
            case PropDefType::Ammo:
            case PropDefType::Armour:
                return true;
            default:
                return false;
        }
    }
    // objIsMortal: whether the object can be damaged at all.
    static bool IsMortal(const PlacedObject& object) {
        if (object.type == static_cast<uint8_t>(PropDefType::Door)) {
            return false;
        }
        if (IsCollectable(object.type) && object.type != static_cast<uint8_t>(PropDefType::Armour)) {
            if ((object.flags & ObjectFlag::ForceMortal) == 0) {
                return false;
            }
        } else if ((object.flags & ObjectFlag::Invincible) != 0) {
            return false;
        }
        return true;
    }

    // chrobjGetBboxFromObjectRecord: the object model's bounding box, null for
    // a model with none.
    const ModelBounds* BoundsOf(const PlacedObject& object) const {
        return Bounds(object);
    }

    // objDrop's placement: the object stands in the world at `position` with the
    // rotation `matrix`, on tile `stan`, shaded by it.
    void Place(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan) {
        SetPosition(object, position, matrix, stan);
        Moved(object);
    }
    // chrobjCollisionRelated and setupUpdateObjectRoomPosition: refreshes
    // `object`'s footprint and rooms after it moved.
    void Moved(PlacedObject* object) {
        UpdateCollision(object);
        UpdateRooms(object);
    }
    // objChangeShading.
    void SetPosition(PlacedObject* object, const Vec3& position, const Mtxf& matrix, int stan);
    // chrobjCollisionRelated.
    void UpdateCollision(PlacedObject* object);
    // setupUpdateObjectRoomPosition.
    void UpdateRooms(PlacedObject* object);
    // sub_GAME_7F0402B4: eases `object` towards the shading of the tile it
    // stands on, unless it is illuminated.
    void UpdateShading(PlacedObject* object);

    // What objDetach does besides unlink the prop: the character that held it
    // forgets it. Called before the prop is detached.
    void SetDetachHook(std::function<void(int prop)> hook) {
        detachHook_ = std::move(hook);
    }
    // sub_GAME_7F050DE8: the object's deformed vertices are given back.
    void SetFreeDeformHook(std::function<void(PlacedObject&)> hook) {
        freeDeform_ = std::move(hook);
    }
    // The pool of projectile records (g_Projectiles) and of embedments
    // (g_Embedments).
    Projectile& ProjectileAt(int slot) {
        return projectiles_[static_cast<size_t>(slot)];
    }
    const Projectile& ProjectileAt(int slot) const {
        return projectiles_[static_cast<size_t>(slot)];
    }
    // The record `object` flies with: its embedment's while embedded, else its
    // own while ObjectRuntime::HasProjectile is set; null for neither.
    Projectile* ProjectileOf(PlacedObject& object);
    // projectileAllocate: a free record, else the oldest in use (its object is
    // removed); -1 when none is.
    int AllocateProjectile();
    // projectileFree.
    void FreeProjectile(int slot);
    // sub_GAME_7F03FDA8: the object of `prop` gets a projectile record unless
    // it has one.
    void GiveProjectile(int prop);
    // projectileSetSticky.
    void SetSticky(int prop);
    // objFreeEmbedmentOrProjectile.
    void FreeEmbedmentOrProjectile(int prop);
    // embedmentAllocate and embedmentFree: whether one was free.
    bool AllocateEmbedment();
    void FreeEmbedment();

    // propExecuteTickOperation for a pickup's outcome: TICKOP_FREE takes the
    // prop out of the world and frees it, TICKOP_GIVETOPLAYER hands it to the
    // player's prop, out of the world too.
    void Free(int prop);
    void GiveToPlayer(int prop, int playerProp);

  private:
    // propPickupByPlayer's tail: a freed pickup of a tagged object is kept.
    PickupOutcome Finish(const PlacedObject& object, bool freed) const;
    // bondinvAddWeaponByProp: whether the pickup added an inventory entry.
    bool AddWeaponByProp(const StageSetup& setup, Inventory* inventory, PlacedObject* object);
    // Adds `object` to the stage's objects and its prop's link to it.
    PlacedObject& Store(const PlacedObject& object);
    // weaponAssignToHome's assigned-to-character branch and
    // sub_GAME_7F051F30 (chrEquipWeapon): the weapon of propdef record `def`,
    // held by `host`.
    bool AssignWeapon(const PropDef& def, size_t defIndex, const PropModel& model, const ChrHost& host);
    // sub_GAME_7F051F30 and chrEquipWeapon for an object `object` describes:
    // makes its model, hangs it on the character and reparents its prop.
    bool EquipHeldWeapon(PlacedObject object, const PropModel& model, float extraScale, const ChrHost& host);
    // hatApplyToChr for a hat `object` describes.
    bool PutOnHat(PlacedObject object, const PropModel& model, float extraScale, const ChrHost& host);
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
    // sub_GAME_7F03FAB0: the object whose footprint holds `position`, among
    // those standing in `room`.
    const PlacedObject* ObjectBeneath(const Vec3& position, int room) const;
    // walkTilesBetweenPoints_NoCallback from a tile that may be missing.
    bool Walk(int* tile, const Vec3& from, const Vec3& to) const;

    std::deque<PlacedObject> objects_;
    std::array<Projectile, kMaxProjectiles> projectiles_;
    int embedmentsUsed_ = 0;
    // The index in objects_ of the object each propdef record made, -1 for none.
    std::vector<int> byDef_;
    std::function<bool(PropCollider*)> viewerFootprint_;
    std::function<bool(size_t)> safeCheck_;
    std::function<bool(int, BodyBounds*)> bodyBounds_;
    std::function<bool(int, int)> doorPasses_;
    std::function<bool(int, int, PropCollider*)> chrCollider_;
    std::function<ChrHost(int)> chrHost_;
    std::function<void(int)> detachHook_;
    std::function<void(PlacedObject&)> freeDeform_;
    ModelFactory* models_factory_ = nullptr;
    std::map<std::string, std::optional<TankGeometry>> tankGeometry_;
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

// chrobjTestPointPolygonCollision: whether a circle of `radius` around a
// world position touches a convex polygon's edge.
bool PointPolygonCollision(const Vec3& point, float radius, const std::array<Vec2, 8>& polygon, int edges);

// chrobjTestPolygonsTouchingOrOverlap2D: whether two convex polygons overlap on
// the ground plane.
bool PolygonsOverlap(const std::array<Vec2, 8>& a, int edgesA, const std::array<Vec2, 8>& b, int edgesB);

// g_SoloAmmoMultiplier, as set for `difficulty` (0 to 2, matching
// StageSetup::LoadsObject's) by the difficulty-select screen's
// DEFAULT_AGENT_SOLO_AMMO_MULTIPLIER/DEFAULT_SECRET_AGENT_SOLO_AMMO_MULTIPLIER/
// DEFAULT_00_AGENT_SOLO_AMMO_MULTIPLIER. That screen isn't ported yet, so
// callers without a real difficulty selection should pass 1.0 (00 Agent's
// value) explicitly rather than call this.
float SoloAmmoMultiplier(int difficulty);

} // namespace Frigate::Level
