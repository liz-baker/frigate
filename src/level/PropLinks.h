#pragma once

#include <cstddef>
#include <vector>

#include "level/Doors.h"
#include "level/Inventory.h"
#include "level/Objects.h"
#include "level/StageSetup.h"

namespace Frigate::Level {

// The object or door that propdef record `def` made, when it made one.
struct LinkTarget {
    PlacedObject* object = nullptr;
    PlacedDoor* door = nullptr;

    bool Exists() const {
        return object != nullptr || door != nullptr;
    }
    int Prop() const {
        return object != nullptr ? object->prop : (door != nullptr ? door->prop : -1);
    }
    uint32_t& Flags2() const {
        return object != nullptr ? object->flags2 : door->flags2;
    }
    uint32_t& Runtime() const {
        return object != nullptr ? object->runtimeFlags : door->runtimeFlags;
    }
};

// The propdef records that act on other records rather than creating
// objects: tag, rename, link, switch, lock door and safe item
// (docs/formats/goldeneye-stage-setup.md). Targets are found the way
// setupCommandGetObject finds them, by an offset from the record's own index.
class PropLinks final {
  public:
    // Forgets every link.
    void Reset();

    // The passes proplvreset2 makes over the records once every object and
    // door exists: a tag marks its object, a rename marks the object it
    // renames, and link, switch, safe item and lock door join their targets.
    void Build(const StageSetup& setup, Objects* objects, Doors* doors);

    // bondinvAddTextOverride for each rename record, in the setup's order.
    static void AddTextOverrides(const StageSetup& setup, Inventory* inventory);

    // objFindByTagId: the object or door the tag record numbered `tagId` marks;
    // empty when no such tag exists or its target was not created.
    LinkTarget FindByTag(uint16_t tagId) const;
    // sub_GAME_7F057080 and setupGetPtrToCommandByIndex: the propdef record the
    // tag numbered `tagId` points at, whatever it is; null when there is no
    // such tag.
    const PropDef* FindTaggedRecord(uint16_t tagId) const;
    // sub_GAME_7F03E6A0: the door every switch record joins `prop` to is
    // activated.
    void TriggerSwitches(int prop, Doors* doors) const;
    // doorIsPadlockFree's search: whether the door made from propdef record
    // `doorDef` has a padlock that is still intact.
    bool IsLocked(size_t doorDef) const;
    // objCanPickupFromSafe: false while the item made from `itemDef` is locked
    // in a safe whose door is shut.
    bool CanPickupFromSafe(size_t itemDef) const;

  private:
    struct Switch {
        // The props that activate and the door prop that follows.
        int first = -1;
        int second = -1;
    };
    struct Lock {
        size_t doorDef = 0;
        size_t lockDef = 0;
    };
    struct SafeItem {
        size_t itemDef = 0;
        size_t doorDef = 0;
    };

    struct Tag {
        uint16_t id = 0;
        // The propdef record the tag marks.
        int64_t target = -1;
    };

    std::vector<Tag> tags_;
    std::vector<Switch> switches_;
    std::vector<Lock> locks_;
    std::vector<SafeItem> safeItems_;
    Objects* objects_ = nullptr;
    Doors* doors_ = nullptr;
    const StageSetup* setup_ = nullptr;
};

} // namespace Frigate::Level
