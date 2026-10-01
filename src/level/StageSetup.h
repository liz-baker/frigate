#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "level/AiLists.h"
#include "level/BgHeader.h"
#include "level/Inventory.h"
#include "level/Stan.h"
#include "level/Waypoints.h"

namespace Frigate::Level {

// The values the game uses for the setup file's intro record types
// (INTROTYPE).
enum class IntroType : int32_t {
    Spawn,
    Item,
    Ammo,
    Swirl,
    Anim,
    Cuff,
    Camera,
    Watch,
    Credits,
    End,
};

// The values the game uses for PROPDEF_TYPE, the type byte of each record in
// the setup file's propdef section.
enum class PropDefType : uint8_t {
    Nothing,
    Door,
    DoorScale,
    Prop,
    Key,
    Alarm,
    Cctv,
    Magazine,
    Collectable,
    Guard,
    Monitor,
    MultiMonitor,
    Rack,
    Autogun,
    Link,
    Debris,
    Unk16,
    Hat,
    GuardAttribute,
    Switch,
    Ammo,
    Armour,
    Tag,
    ObjectiveStart,
    ObjectiveEnd,
    ObjectiveDestroyObject,
    ObjectiveCompleteCondition,
    ObjectiveFailCondition,
    ObjectiveCollectObject,
    ObjectiveDepositObject,
    ObjectivePhotograph,
    ObjectiveNull,
    ObjectiveEnterRoom,
    ObjectiveDepositObjectInRoom,
    ObjectiveCopyItem,
    WatchMenuObjectiveText,
    GasReleasing,
    Rename,
    LockDoor,
    Vehicle,
    Aircraft,
    Unk41,
    Glass,
    Safe,
    SafeItem,
    Tank,
    CameraPos,
    TintedGlass,
    End,
};

// One record of the propdef section. On-disk layout:
// docs/formats/goldeneye-stage-setup.md.
struct PropDef {
    // The record's type byte, which may be a value PropDefType does not name.
    uint8_t type = 0;
    // Where the record starts in the file, and its size from sizepropdef.
    uint32_t offset = 0;
    uint32_t words = 0;
    // The ObjectRecord fields, for the types that begin with one (see
    // IsObjectRecord); 0 for the others.
    uint16_t extraScale = 0;
    // The state byte (PROPSTATE_*).
    uint8_t state = 0;
    int16_t model = 0;
    int16_t pad = 0;
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    // ObjectRecord::damage, the health the object starts with, converted from
    // the file's fixed point as domakedefaultobj does.
    float damage = 0;
    // TintedGlassRecord's fields past the ObjectRecord, valid only when type is
    // PropDefType::TintedGlass: how far from the player the glass is clear
    // (TintDist) and opaque (CullDist), and unk90 as the file holds it (a
    // fixed-point opacity once the glass has a portal, else read as a float).
    float tintDistance = 0;
    float cullDistance = 0;
    uint32_t tintOpacityWord = 0;

    // DoorRecord's own fields, valid only when type is PropDefType::Door.
    // linkedDoorOffset: another door record's index, relative to this one's,
    // that also activates when this door does; 0 for none.
    int32_t doorLinkedOffset = 0;
    // Fixed-point fields already converted to float, as setupDoor does at
    // load time.
    float doorMaxFrac = 0;
    float doorPerimFrac = 0;
    float doorAccel = 0;
    float doorDecel = 0;
    float doorMaxSpeed = 0;
    // DOORFLAG_* and DOORTYPE_*.
    uint16_t doorFlags = 0;
    uint16_t doorType = 0;
    uint32_t doorKeyFlags = 0;
    uint32_t doorAutoCloseFrames = 0;
    uint32_t doorOpenSound = 0;

    // WeaponObjRecord's fields past the ObjectRecord header, valid only when
    // type is PropDefType::Collectable.
    int32_t weaponNum = kItemNothing;
    // -1 for none; otherwise another ITEM_IDS this collectable pairs with in
    // the inventory as a dual-wielded weapon (bondinvAddWeaponByProp).
    int32_t weaponLinkedType = -1;

    // AmmoCrateRecord::ammoType, valid only when type is PropDefType::Magazine.
    int32_t ammoType = 0;

    // MonitorObjRecord's OwnerOffset and OwnerPart, valid only when type is
    // PropDefType::Monitor: the record (relative to this one) whose model the
    // monitor sits on when it has no pad, and the switch of that model.
    int32_t monitorOwnerOffset = 0;
    int32_t monitorOwnerPart = 0;

    // MultiAmmoCrateRecord's slots, valid only when type is
    // PropDefType::Ammo. Slot i grants ammo type i + 1 (AMMO_9MM..AMMO_GGUN).
    static constexpr size_t kAmmoSlotCount = 13;
    struct AmmoSlot {
        uint16_t model = 0xFFFF;
        uint16_t quantity = 0;
    };
    std::array<AmmoSlot, kAmmoSlotCount> ammoSlots{};

    // BodyArmourRecord::initialamount, already converted from fixed point;
    // valid only when type is PropDefType::Armour.
    float armourAmount = 0;

    // KeyRecord::keyflags, valid only when type is PropDefType::Key.
    uint32_t keyFlags = 0;

    // GuardRecord's fields, valid only when type is PropDefType::Guard.
    uint16_t guardChrnum = 0;
    uint16_t guardPad = 0;
    // A BODY_* value; 0xFFFF picks the stage's random body.
    uint16_t guardBody = 0;
    uint16_t guardAiList = 0;
    uint16_t guardPreset = 0;
    uint16_t guardChrPreset1 = 0;
    uint16_t guardHealth = 0;
    uint16_t guardReactionTime = 0;
    // GUARD_SETUP_FLAG_*.
    uint16_t guardFlags = 0;
    // A HEAD_* value; negative picks a random head.
    int16_t guardHead = 0;

    // VehichleRecord's and AircraftRecord's ailist, which the file holds as the
    // AI list's id; valid only when type is PropDefType::Vehicle or Aircraft.
    int32_t objectAiList = 0;

    // GuardAttributeRecord's fields, valid only when type is
    // PropDefType::GuardAttribute.
    int32_t attributeChrnum = 0;
    uint8_t grenadeProb = 0;

    // The records that point at other records hold each target as an offset
    // from their own index: Index1 to Index3 of LinkRecord (Link: the two
    // weapons that pair; Switch: the object, then the door it also opens),
    // LockDoorRecord (the door, then its padlock) and SafeObjectRecord (the
    // item, the safe, then the door).
    std::array<int32_t, 3> refs{};
    // TagObjectRecord::ID and ::OffsetToObj, valid only when type is
    // PropDefType::Tag. The AI commands name an object by the id.
    uint16_t tagId = 0;
    int16_t tagOffset = 0;

    // RenameObjectRecord's textoverride fields, valid only when type is
    // PropDefType::Rename. renameOffset is the target object's offset from
    // the record; 0 makes the text apply to the weapon id `renameWeapon`
    // instead.
    int32_t renameOffset = 0;
    int32_t renameWeapon = 0;
    uint32_t renameTitle1 = 0;
    uint32_t renameTitle2 = 0;
    uint32_t renameShortText = 0;
    uint32_t renameLongText = 0;
    uint32_t renamePickupText = 0;

    bool Is(PropDefType other) const {
        return type == static_cast<uint8_t>(other);
    }
};

// Whether records of `type` start with the ObjectRecord fields.
bool IsObjectRecord(uint8_t type);

// sizepropdef: the record's size in 32-bit words.
uint32_t PropDefWords(uint8_t type);

// The counts proplvreset2 works out before it creates anything, from
// load_proptype.
struct PropCounts {
    // Guards (alloc_init_GUARDdata_entries).
    int guards = 0;
    // The inventory's extra item slots (alloc_additional_item_slots).
    int links = 0;
    // The model slots (modelmgrAllocateModelSlots) and the animated ones
    // (modelmgrAllocateAnimModelSlots).
    int objects = 0;
    int animatedObjects = 0;
};

// PadRecord. On-disk layout: docs/formats/goldeneye-stage-setup.md.
struct Pad {
    Vec3 position{};
    Vec3 up{};
    Vec3 look{};
    // plink: the name of the stand tile the pad sits on.
    std::string tileName;
    // stan: the tile the pad stands on, once StageSetup::ResolveStanTiles has
    // run; kNoTile when no tile holds it.
    int stanTile = kNoTile;
};

// BoundPadRecord: a pad with the box a prop assigned to it fills.
struct BoundPad {
    Pad pad;
    // xmin, xmax, ymin, ymax, zmin, zmax.
    std::array<float, 6> box{};
};

// The intro records. The `demoSlot` of a spawn, item or ammo record says which
// demo it applies to; 0 is normal play.
struct IntroSpawn {
    int32_t pad = 0;
    int32_t demoSlot = 0;
};
struct IntroItem {
    int32_t itemRight = 0;
    // -1 when the item is not a dual pair.
    int32_t itemLeft = -1;
    int32_t demoSlot = 0;
};
struct IntroAmmo {
    int32_t ammoType = 0;
    int32_t amount = 0;
    int32_t demoSlot = 0;
};
struct IntroSwirl {
    int32_t flags = 0;
    // Offset from Bond in centimetres.
    Vec3 offset{};
    float scale = 0;
    float duration = 0;
    // The pad the swirl looks at.
    int32_t pad = 0;
};
struct IntroAnim {
    int32_t animation = 0;
};
struct IntroCuff {
    int32_t bondType = 0;
};
struct IntroCamera {
    Vec3 position{};
    // Radians.
    float yaw = 0;
    float pitch = 0;
    int32_t pad = 0;
    // The caption's text ids (lang1c.lang_index; the game looks up the
    // second) and the optional second caption (lang20; 0 = none).
    std::array<uint16_t, 2> caption{};
    int32_t secondCaption = 0;
};
struct IntroWatch {
    int32_t hours = 0;
    int32_t minutes = 0;
};
struct IntroCredits {
    // Offset of the credits table in the file.
    uint32_t tableOffset = 0;
};

using IntroRecord = std::variant<IntroSpawn, IntroItem, IntroAmmo, IntroSwirl, IntroAnim, IntroCuff, IntroCamera,
                                 IntroWatch, IntroCredits>;

// A stage's setup file, after what proplvreset2 does to it while loading.
class StageSetup {
  public:
    // `file` is the decompressed setup file and `roomScale` the stage's
    // get_room_data_float2(), 1 / level scale. Empty when the file is
    // truncated or its sections run off the end; `error` says why.
    static std::optional<StageSetup> Parse(const std::vector<uint8_t>& file, float roomScale, std::string* error);

    // How init_pathtable_something resolved each pad and bound pad.
    struct StanResolution {
        // The named tile held the pad.
        int named = 0;
        // Another tile was found (the game's debug build prints "changed stan").
        int moved = 0;
        // No tile was found ("has no stan").
        int missing = 0;
    };
    // proplvreset2's init_pathtable_something pass: finds the stan tile of
    // every pad and bound pad.
    StanResolution ResolveStanTiles(Stan& stan);

    // The section offsets the header holds, in header order.
    const std::array<uint32_t, 10>& Header() const {
        return header_;
    }
    const std::vector<Pad>& Pads() const {
        return pads_;
    }
    const std::vector<BoundPad>& BoundPads() const {
        return boundPads_;
    }
    // Empty when the file has no name list.
    const std::vector<std::string>& PadNames() const {
        return padNames_;
    }
    const std::vector<std::string>& BoundPadNames() const {
        return boundPadNames_;
    }
    // The intro section's records in file order, up to the end record.
    const std::vector<IntroRecord>& Intro() const {
        return intro_;
    }
    // The propdef section's records in file order, up to the end record.
    const std::vector<PropDef>& PropDefs() const {
        return propDefs_;
    }
    // The waypoint graph, patrol paths and AI lists of the file, as proplvreset2
    // leaves them. The sections are present when the header names them.
    const std::vector<Waypoint>& PathWaypoints() const {
        return waypoints_;
    }
    const std::vector<Waygroup>& WaypointGroups() const {
        return waygroups_;
    }
    const std::vector<PatrolPath>& PatrolPaths() const {
        return patrolPaths_;
    }
    bool HasWaypointSection() const {
        return header_[0] != 0;
    }
    bool HasWaygroupSection() const {
        return header_[1] != 0;
    }
    const std::vector<AiList>& AiListSection() const {
        return aiLists_;
    }
    // g_DoorScale: what the last DoorScale record says, 1 without one.
    float DoorScale() const {
        return doorScale_;
    }
    // load_proptype: how many records have `type`.
    int Count(PropDefType type) const;
    // The counts proplvreset2 sizes the object and guard tables with.
    PropCounts Counts() const;
    // Whether proplvreset2 creates the object `def` on `difficulty` (0 to 2)
    // with `playerCount` players: not when its flags2 hold that difficulty's
    // or that player count's "don't load" bit.
    static bool LoadsObject(const PropDef& def, int difficulty, int playerCount);

  private:
    std::array<uint32_t, 10> header_{};
    std::vector<Pad> pads_;
    std::vector<BoundPad> boundPads_;
    std::vector<std::string> padNames_;
    std::vector<std::string> boundPadNames_;
    std::vector<IntroRecord> intro_;
    std::vector<PropDef> propDefs_;
    std::vector<Waypoint> waypoints_;
    std::vector<Waygroup> waygroups_;
    std::vector<PatrolPath> patrolPaths_;
    std::vector<AiList> aiLists_;
    float doorScale_ = 1.0f;
};

} // namespace Frigate::Level
