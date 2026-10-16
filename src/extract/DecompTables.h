#pragma once

#include "extract/Region.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The tables generated from the GoldenEye decomp's sources by tools/gen_tables.py (docs/extract.md). The row types are
// declared here; the data is in the *Data.cpp files, which are committed so building needs no decomp checkout.
// Symbolic names (BODY_/HEAD_/PROP_/ITEM_/AMMO names, animation and monitor list names) are kept as strings.
namespace Frigate::Extract {

// One MODELFILEHEADER of the chr, gun and prop tables, sorted by name for bsearch-style lookup.
struct ModelHeader {
    const char* name;
    int32_t numSwitches;
    int32_t numTextures;
    const char* skeleton; // the SKELETON(name) argument, nullptr for none
    double radius;
};

// One joint of a skeleton's JOINTLIST: its node type and the two matrix indices.
struct SkeletonJoint {
    uint16_t nodeType;
    uint16_t matrixA;
    uint16_t matrixB;
};

// A skeleton's slice of kSkeletonJoints, sorted by name.
struct SkeletonEntry {
    const char* name;
    uint32_t firstJoint;
    uint32_t jointCount;
};

// One slot of the chr model table (c_item_entries); the array index is the slot. `names` is the comma-separated
// BODY_/HEAD_ identifiers of the slot, the scales are the decomp's own text.
struct ChrRecord {
    const char* names;
    const char* file;
    int32_t isMale;
    int32_t hasHead;
    const char* scale;
    const char* pov;
};

// A hat placement as the decomp writes it: x, y and z offsets, then x, y and z sizes.
struct ChrHat {
    const char* values[6];
};

// One PROP value's model record. The model file is P<model>Z; `scale` is the decomp's own text.
struct PropRecord {
    const char* name; // PROP_...
    const char* model;
    const char* scale;
    int32_t explosionType;
    int32_t seeds[6];
};

struct WeaponFiring {
    int32_t automaticRate;
    int32_t singleRate;
    int32_t recoilSpeed[4];
    double recoilBack;
    double recoilUp;
    const char* cartridge; // resource base path of the cartridge model, nullptr for none
    // LoudnessMin, LoudnessMax, NoiseIncreasePerShot, NoiseDecayLinearTime, NoiseDecayScaledTime, Zoom, Inaccuracy,
    // ObjectsShootThrough, DestructionAmount
    double floats[9];
    int32_t soundTriggerRate;
    int32_t sound;
};

struct WeaponItem {
    const char* name; // ITEM_...
    int32_t hasHeader;
    int32_t ammoType;
    int32_t magazine;
    uint32_t flags;
    const char* model;    // resource base path, nullptr for an item with no model file
    const char* skeleton; // nullptr for none
    // MuzzleFlashExtension, PosX/Y/Z, PlayX/Y/Z, CrosshairSpeed, AimLockSpeed, Sway, BoltRecoilBack
    double placement[11];
    WeaponFiring firing;
    double forceOfImpact;
};

struct WeaponWatch {
    uint32_t upperText;
    uint32_t lowerText;
    double placement[5]; // position x/y/z, rotation x/y
    uint32_t choiceText;
    uint32_t equipmentText;
    double equip[3];
};

struct WeaponAmmo {
    const char* name; // AMMOTYPE enumerator, nullptr for a type the enum does not name
    int32_t limit;
    uint32_t icon;
    double iconYOffset;
};

// The weapon table of one region; the item, watch and ammo rows are in ITEM_IDS / ammo type order.
struct WeaponTables {
    const WeaponItem* items;
    size_t itemCount;
    const WeaponWatch* watch;
    size_t watchCount;
    const WeaponAmmo* ammo;
    size_t ammoCount;
};

struct StruckAnim {
    const char* list;
    uint32_t index;
    const char* anim;
    int32_t flip;
    double endFrame;
    double speed;
    int32_t knockback;
    double thudFrame1;
    double thudFrame2;
};

struct HitReaction {
    int32_t hitPart;
    int32_t impactPuffs;
    double impactSize;
    int32_t backPuffs;
    double backSize;
    const char* deathList; // nullptr for none
    const char* flinchList;
};

struct FiringAnim {
    const char* table;
    uint32_t index;
    const char* anim;
    double values[17];
};

struct FiringGroup {
    const char* set;
    uint32_t index;
    const char* table;
};

struct ExplosionDeath {
    const char* anim;
    int32_t flip;
    double values[5]; // speed, start frame, the unused frame, thud frame, end frame
};

struct ExplosionGroup {
    uint32_t count;
    int32_t deaths[16];
};

// A multiplayer firing animation: a firing table entry (`table`, `index`) or a bare animation (`table` is nullptr).
struct MultiFiring {
    const char* table;
    uint32_t index;
    const char* anim;
    double x;
    double y;
    double z;
};

struct AnimationId {
    int32_t table; // 1: characters, 2: aircraft
    uint32_t id;
    const char* name; // nullptr for an id with no clip
};

// A word of a monitor command list: a number, or a reference to another list by name.
struct MonitorWord {
    const char* list; // nullptr for a number
    uint32_t value;
};
constexpr MonitorWord Num(uint32_t value) {
    return { nullptr, value };
}
constexpr MonitorWord Ref(const char* list) {
    return { list, 0 };
}

struct MonitorList {
    const char* name;
    std::span<const MonitorWord> words;
};

struct MonitorImage {
    uint32_t number;
    const char* list;
};

struct RamromDemo {
    const char* name;
    int32_t locked;
};

namespace Detail {
extern const ModelHeader kModelHeaders[];
extern const size_t kModelHeaderCount;
extern const SkeletonJoint kSkeletonJoints[];
extern const SkeletonEntry kSkeletons[];
extern const size_t kSkeletonCount;
extern const ChrRecord kChrRecords[];
extern const size_t kChrRecordCount;
extern const ChrHat kChrHats[];
extern const size_t kChrHatCount;
extern const PropRecord kPropRecords[];
extern const size_t kPropRecordCount;
extern const WeaponTables kWeaponTablesUs;
extern const WeaponTables kWeaponTablesEur;
extern const WeaponTables kWeaponTablesJp;
extern const StruckAnim kStruckAnims[];
extern const size_t kStruckAnimCount;
extern const HitReaction kHitReactions[];
extern const size_t kHitReactionCount;
extern const FiringAnim kFiringAnims[];
extern const size_t kFiringAnimCount;
extern const FiringGroup kFiringGroups[];
extern const size_t kFiringGroupCount;
extern const ExplosionDeath kExplosionDeaths[];
extern const size_t kExplosionDeathCount;
extern const ExplosionGroup kExplosionGroups[];
extern const size_t kExplosionGroupCount;
extern const MultiFiring kMultiFiring[];
extern const size_t kMultiFiringCount;
extern const AnimationId kAnimationIds[];
extern const size_t kAnimationIdCount;
extern const MonitorList kMonitorLists[];
extern const size_t kMonitorListCount;
extern const char* const kMonitorDefaultList;
extern const MonitorImage kMonitorImages[];
extern const size_t kMonitorImageCount;
extern const RamromDemo kRamromDemos[];
extern const size_t kRamromDemoCount;
} // namespace Detail

// The weapon tables of a region (they differ in a few scale and rate constants).
const WeaponTables& GetWeaponTables(Region region);

} // namespace Frigate::Extract
