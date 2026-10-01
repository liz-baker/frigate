#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Frigate::Level {

// The values the game uses for ITEM_IDS that stage load names.
constexpr int32_t kItemNothing = -1;
constexpr int32_t kItemUnarmed = 0;
constexpr int32_t kItemFist = 1;

// One entry of a player's inventory: a single weapon, a dual-wield pair, or a
// prop the player picked up and kept (a key, a hat or any other object that
// falls through propPickupByPlayer's default case).
struct InvItem {
    int32_t right = kItemNothing;
    // kItemNothing for a single weapon.
    int32_t left = kItemNothing;
    // INV_ITEM_PROP: the kept prop's slot in the prop list, and the record
    // the object was made from (an index into the setup's propdefs), which its
    // text override is looked up by. -1 for an entry that is not a prop.
    int prop = -1;
    int64_t object = -1;

    bool IsProp() const {
        return prop >= 0;
    }
    bool IsDual() const {
        return !IsProp() && left != kItemNothing;
    }
};

// textoverride: the names and pickup text a RENAME record gives an object or
// a weapon id (bondinvAddTextOverride).
struct TextOverride {
    // The record of the object the text belongs to, -1 for a weapon id's, or
    // kUnresolved for a rename whose target offset names no object.
    int64_t object = -1;
    // The weapon id a prop-backed entry stands for in the inventory, and the
    // id a weapon-id override applies to.
    int32_t weapon = 0;
    // The string id the pickup message says, 0 for none.
    int32_t pickupText = 0;
    // The string ids the watch's inventory page shows, 0 for none.
    uint32_t titleText1 = 0;
    uint32_t titleText2 = 0;
    uint32_t shortText = 0;
    uint32_t longText = 0;

    static constexpr int64_t kUnresolved = -2;
};

// The player's weapon inventory (bondinv.c), which the game keeps as a
// circular list sorted by weapon id.
class Inventory {
  public:
    // bondinvAddInvItem: true if the item was added.
    bool AddItem(int32_t item);
    // bondinvAddDoublesInvItem.
    bool AddDoubles(int32_t right, int32_t left);
    // bondinvAddPropToInv: keeps the prop `prop`, made from record `object`.
    void AddProp(int prop, int64_t object);
    // bondinvHasInvItem.
    bool HasItem(int32_t item) const;
    // bondinvHasDualWeapon.
    bool HasDual(int32_t right, int32_t left) const;

    // bondinvCheckHasKeyFlags: whether the keys kept together hold every bit of
    // `wanted`. `keyFlagsOf` gives the key flags of the propdef record an
    // entry was made from, 0 for one that is not a key.
    bool HasKeyFlags(uint32_t wanted, const std::function<uint32_t(int64_t)>& keyFlagsOf) const;

    // bondinvAddTextOverride, bondinvGetTextbyObj and
    // bondinvGetTextbyWeaponID.
    void AddTextOverride(const TextOverride& override);
    const TextOverride* TextByObject(int64_t object) const;
    const TextOverride* TextByWeapon(int32_t weapon) const;

    // In the order the game cycles them, starting from ptr_inventory_first_in_cycle.
    const std::vector<InvItem>& Items() const {
        return items_;
    }

    // bondinvGetTextbyInvIndex: the weapon id an inventory-cycle index
    // displays: a single weapon's, or a kept prop's text override's. 0
    // (ITEM_UNARMED) for anything else.
    // TODO(https://github.com/liz-baker/frigate/issues/136): the cheat that
    // makes every gun available reads the index as a weapon id.
    int32_t TextIndex(size_t index) const;

    // bondinvReinitInv.
    void Clear() {
        items_.clear();
        overrides_.clear();
    }

  private:
    // bondinvInsertItem and bondinvSortInv.
    void Insert(const InvItem& item);

    std::vector<InvItem> items_;
    std::vector<TextOverride> overrides_;
};

} // namespace Frigate::Level
