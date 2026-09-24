#pragma once

#include <cstdint>
#include <vector>

namespace Frigate::Level {

// The values the game uses for ITEM_IDS that stage load names.
constexpr int32_t kItemNothing = -1;
constexpr int32_t kItemUnarmed = 0;
constexpr int32_t kItemFist = 1;

// One entry of a player's inventory: a single weapon, or a dual-wield pair.
struct InvItem {
    int32_t right = kItemNothing;
    // kItemNothing for a single weapon.
    int32_t left = kItemNothing;

    bool IsDual() const {
        return left != kItemNothing;
    }
};

// The player's weapon inventory (bondinv.c), which the game keeps as a
// circular list sorted by weapon id.
class Inventory {
  public:
    // bondinvAddInvItem: true if the item was added.
    bool AddItem(int32_t item);
    // bondinvAddDoublesInvItem.
    bool AddDoubles(int32_t right, int32_t left);
    // bondinvHasInvItem.
    bool HasItem(int32_t item) const;
    // bondinvHasDualWeapon.
    bool HasDual(int32_t right, int32_t left) const;

    // In the order the game cycles them, starting from ptr_inventory_first_in_cycle.
    const std::vector<InvItem>& Items() const {
        return items_;
    }

  private:
    // bondinvInsertItem and bondinvSortInv.
    void Insert(const InvItem& item);

    std::vector<InvItem> items_;
};

} // namespace Frigate::Level
