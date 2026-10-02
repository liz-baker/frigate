#include "level/PropLinks.h"

namespace Frigate::Level {

namespace {

// setupCommandGetObject's answer: the records that are objects.
bool IsLinkTargetable(uint8_t type) {
    switch (static_cast<PropDefType>(type)) {
        case PropDefType::Door:
        case PropDefType::Prop:
        case PropDefType::Key:
        case PropDefType::Alarm:
        case PropDefType::Cctv:
        case PropDefType::Magazine:
        case PropDefType::Collectable:
        case PropDefType::Monitor:
        case PropDefType::MultiMonitor:
        case PropDefType::Rack:
        case PropDefType::Autogun:
        case PropDefType::Hat:
        case PropDefType::Ammo:
        case PropDefType::Armour:
        case PropDefType::GasReleasing:
        case PropDefType::Vehicle:
        case PropDefType::Aircraft:
        case PropDefType::Unk41:
        case PropDefType::Glass:
        case PropDefType::Safe:
        case PropDefType::Tank:
        case PropDefType::TintedGlass:
            return true;
        default:
            return false;
    }
}

class Records {
  public:
    Records(const StageSetup& setup, Objects* objects, Doors* doors) : setup_(setup), objects_(objects), doors_(doors) {
    }

    // setupGetPtrToCommandByIndex: the record `index` places from the
    // start of the section, null outside it.
    const PropDef* Record(int64_t index) const {
        if (index < 0 || static_cast<size_t>(index) >= setup_.PropDefs().size()) {
            return nullptr;
        }
        return &setup_.PropDefs()[static_cast<size_t>(index)];
    }

    // setupCommandGetObject, for a record that was created.
    LinkTarget Object(int64_t index) const {
        const PropDef* record = Record(index);
        if (record == nullptr || !IsLinkTargetable(record->type)) {
            return {};
        }
        return Created(static_cast<size_t>(index));
    }

    LinkTarget Created(size_t def) const {
        LinkTarget target;
        if (setup_.PropDefs()[def].Is(PropDefType::Door)) {
            target.door = doors_->ForDef(def);
        } else {
            target.object = objects_->ForDef(def);
        }
        return target;
    }

  private:
    const StageSetup& setup_;
    Objects* objects_;
    Doors* doors_;
};

} // namespace

void PropLinks::Reset() {
    tags_.clear();
    switches_.clear();
    locks_.clear();
    safeItems_.clear();
    objects_ = nullptr;
    doors_ = nullptr;
}

void PropLinks::Build(const StageSetup& setup, Objects* objects, Doors* doors) {
    Reset();
    objects_ = objects;
    doors_ = doors;
    setup_ = &setup;
    const Records records(setup, objects, doors);
    const auto& defs = setup.PropDefs();

    // The first pass of the records.
    for (size_t index = 0; index < defs.size(); index++) {
        const PropDef& def = defs[index];
        if (def.Is(PropDefType::Tag)) {
            const LinkTarget tagged = records.Object(static_cast<int64_t>(index) + def.tagOffset);
            if (tagged.Exists()) {
                tagged.Runtime() |= ObjectRuntime::Tagged;
            }
            // set_parent_cur_tag_entry: the newest tag is searched first.
            tags_.push_back({ def.tagId, static_cast<int64_t>(index) + def.tagOffset });
        } else if (def.Is(PropDefType::Rename)) {
            const LinkTarget renamed = records.Object(static_cast<int64_t>(index) + def.renameOffset);
            if (renamed.Exists()) {
                renamed.Runtime() |= ObjectRuntime::Destroyed;
            }
        }
    }

    // The second pass, which needs every target to have its prop.
    for (size_t index = 0; index < defs.size(); index++) {
        const PropDef& def = defs[index];
        const auto at = [&](int32_t offset) { return static_cast<int64_t>(index) + offset; };
        if (def.Is(PropDefType::Link)) {
            const PropDef* first = records.Record(at(def.refs[0]));
            const PropDef* second = records.Record(at(def.refs[1]));
            if (first != nullptr && second != nullptr && first->Is(PropDefType::Collectable) &&
                second->Is(PropDefType::Collectable)) {
                objects->SetDual(static_cast<size_t>(at(def.refs[0])), first->weaponNum,
                                 static_cast<size_t>(at(def.refs[1])), second->weaponNum);
            }
        } else if (def.Is(PropDefType::Switch)) {
            const LinkTarget first = records.Object(at(def.refs[0]));
            const PropDef* door = records.Record(at(def.refs[1]));
            if (first.Exists() && first.Prop() >= 0 && door != nullptr && door->Is(PropDefType::Door)) {
                const LinkTarget second = records.Created(static_cast<size_t>(at(def.refs[1])));
                if (second.Exists() && second.Prop() >= 0) {
                    switches_.push_back({ first.Prop(), second.Prop() });
                    first.Runtime() |= ObjectRuntime::LinkedToSwitch;
                }
            }
        } else if (def.Is(PropDefType::SafeItem)) {
            const LinkTarget item = records.Object(at(def.refs[0]));
            const LinkTarget safe = records.Object(at(def.refs[1]));
            const LinkTarget door = records.Object(at(def.refs[2]));
            if (item.Exists() && item.Prop() >= 0 && safe.Exists() && safe.Prop() >= 0 &&
                defs[static_cast<size_t>(at(def.refs[1]))].Is(PropDefType::Safe) && door.Exists() && door.Prop() >= 0 &&
                defs[static_cast<size_t>(at(def.refs[2]))].Is(PropDefType::Door)) {
                safeItems_.push_back({ static_cast<size_t>(at(def.refs[0])), static_cast<size_t>(at(def.refs[2])) });
                item.Flags2() |= ObjectFlag2::LinkedToSafe;
                door.Flags2() |= ObjectFlag2::LinkedToSafe;
            }
        } else if (def.Is(PropDefType::LockDoor)) {
            const LinkTarget door = records.Object(at(def.refs[0]));
            const LinkTarget lock = records.Object(at(def.refs[1]));
            if (door.Exists() && door.Prop() >= 0 && lock.Exists() && lock.Prop() >= 0 &&
                defs[static_cast<size_t>(at(def.refs[0]))].Is(PropDefType::Door)) {
                locks_.push_back({ static_cast<size_t>(at(def.refs[0])), static_cast<size_t>(at(def.refs[1])) });
                door.Runtime() |= ObjectRuntime::PadlockedDoor;
            }
        }
    }
}

void PropLinks::AddTextOverrides(const StageSetup& setup, Inventory* inventory) {
    const auto& defs = setup.PropDefs();
    for (size_t index = 0; index < defs.size(); index++) {
        const PropDef& def = defs[index];
        if (!def.Is(PropDefType::Rename)) {
            continue;
        }
        TextOverride text;
        if (def.renameOffset == 0) {
            text.object = -1;
        } else {
            // obj is what setupCommandGetObject answers, which only an object
            // record does; anything else leaves the override matching no object.
            const int64_t target = static_cast<int64_t>(index) + def.renameOffset;
            const bool isObject = target >= 0 && static_cast<size_t>(target) < defs.size() &&
                                  IsLinkTargetable(defs[static_cast<size_t>(target)].type);
            text.object = isObject ? target : TextOverride::kUnresolved;
        }
        text.weapon = def.renameWeapon;
        text.pickupText = static_cast<int32_t>(def.renamePickupText);
        text.titleText1 = def.renameTitle1;
        text.titleText2 = def.renameTitle2;
        text.shortText = def.renameShortText;
        text.longText = def.renameLongText;
        inventory->AddTextOverride(text);
    }
}

LinkTarget PropLinks::FindByTag(uint16_t tagId) const {
    for (auto it = tags_.rbegin(); it != tags_.rend(); ++it) {
        if (it->id != tagId) {
            continue;
        }
        // objFindByTagId: the tagged object, unless the tag was cleared.
        LinkTarget target;
        if (it->target >= 0 && static_cast<size_t>(it->target) < setup_->PropDefs().size()) {
            const size_t def = static_cast<size_t>(it->target);
            if (setup_->PropDefs()[def].Is(PropDefType::Door)) {
                target.door = doors_->ForDef(def);
            } else {
                target.object = objects_->ForDef(def);
            }
        }
        if (target.Exists() && (target.Runtime() & ObjectRuntime::Tagged) == 0) {
            return {};
        }
        return target;
    }
    return {};
}

const PropDef* PropLinks::FindTaggedRecord(uint16_t tagId) const {
    for (auto it = tags_.rbegin(); it != tags_.rend(); ++it) {
        if (it->id == tagId) {
            const auto& defs = setup_->PropDefs();
            return it->target >= 0 && static_cast<size_t>(it->target) < defs.size()
                       ? &defs[static_cast<size_t>(it->target)]
                       : nullptr;
        }
    }
    return nullptr;
}

void PropLinks::TriggerSwitches(int prop, Doors* doors) const {
    for (auto it = switches_.rbegin(); it != switches_.rend(); ++it) {
        if (it->first != prop || it->second < 0) {
            continue;
        }
        if (const PlacedDoor* door = doors->ForProp(it->second)) {
            doors->ActivateWrapper(static_cast<int>(door - doors->All().data()));
        }
    }
}

bool PropLinks::IsLocked(size_t doorDef) const {
    for (const Lock& lock : locks_) {
        if (lock.doorDef != doorDef) {
            continue;
        }
        // The padlock has to still be in the world and unbroken.
        if (const PlacedObject* object = objects_->ForDef(lock.lockDef)) {
            if (object->prop >= 0 && Objects::IsHealthy(*object)) {
                return true;
            }
        } else if (const PlacedDoor* door = doors_->ForDef(lock.lockDef)) {
            if (door->prop >= 0) {
                return true;
            }
        }
    }
    return false;
}

bool PropLinks::CanPickupFromSafe(size_t itemDef) const {
    for (const SafeItem& link : safeItems_) {
        if (link.itemDef != itemDef) {
            continue;
        }
        const PlacedDoor* door = doors_->ForDef(link.doorDef);
        if (door != nullptr && door->prop >= 0 && door->openPosition <= 0.5f) {
            return false;
        }
    }
    return true;
}

} // namespace Frigate::Level
