#include "level/Inventory.h"

namespace Frigate::Level {

bool Inventory::HasItem(int32_t item) const {
    for (const InvItem& entry : items_) {
        if (!entry.IsDual() && entry.right == item) {
            return true;
        }
    }
    return false;
}

bool Inventory::HasDual(int32_t right, int32_t left) const {
    for (const InvItem& entry : items_) {
        if (entry.IsDual() && entry.right == right && entry.left == left) {
            return true;
        }
    }
    return false;
}

void Inventory::Insert(const InvItem& item) {
    // The new entry starts at the head and swaps right until the next entry
    // sorts at or after it.
    size_t at = 0;
    while (at < items_.size()) {
        const InvItem& candidate = items_[at];
        if (candidate.right >= item.right && (item.right != candidate.right || item.left <= candidate.left)) {
            break;
        }
        at++;
    }
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(at), item);
}

bool Inventory::AddItem(int32_t item) {
    if (HasItem(item)) {
        return false;
    }
    // The game's item pool (alloc_additional_item_slots) has a fixed size and
    // drops an add that finds it full; a stage's starting items never do.
    Insert({ item, kItemNothing });
    return true;
}

bool Inventory::AddDoubles(int32_t right, int32_t left) {
    if (HasDual(right, left)) {
        return false;
    }
    Insert({ right, left });
    return true;
}

} // namespace Frigate::Level
