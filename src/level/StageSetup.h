#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "level/BgHeader.h"
#include "level/Stan.h"

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
    float doorScale_ = 1.0f;
};

} // namespace Frigate::Level
