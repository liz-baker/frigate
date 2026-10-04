#include "level/Inventory.h"

#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

// bondinvSortInv's stand-ins for a prop-backed entry: it sorts after every
// weapon as the entry being placed, and before them as one it is compared with.
constexpr int32_t kPropAsSubject = 2000;
constexpr int32_t kPropAsCandidate = 1000;

int32_t FirstKey(const InvItem& item, int32_t propKey) {
    return item.IsProp() ? propKey : item.right;
}

int32_t SecondKey(const InvItem& item) {
    return item.IsProp() ? -1 : item.left;
}

} // namespace

void Inventory::RemoveItem(int32_t item) {
    for (size_t i = 0; i < items_.size(); i++) {
        if (!items_[i].IsProp() && !items_[i].IsDual() && items_[i].right == item) {
            items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
}

bool Inventory::HasItem(int32_t item) const {
    for (const InvItem& entry : items_) {
        if (!entry.IsProp() && !entry.IsDual() && entry.right == item) {
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
    const int32_t subjectFirst = FirstKey(item, kPropAsSubject);
    const int32_t subjectSecond = SecondKey(item);
    size_t at = 0;
    while (at < items_.size()) {
        const InvItem& candidate = items_[at];
        const int32_t candidateFirst = FirstKey(candidate, kPropAsCandidate);
        if (candidateFirst >= subjectFirst &&
            (subjectFirst != candidateFirst || subjectSecond <= SecondKey(candidate))) {
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
    return !(allGuns_ && item < kItemBombCase);
}

bool Inventory::AddDoubles(int32_t right, int32_t left) {
    if (HasDual(right, left)) {
        return false;
    }
    Insert({ right, left });
    return true;
}

void Inventory::AddProp(int prop, int64_t object) {
    InvItem item;
    item.prop = prop;
    item.object = object;
    Insert(item);
}

bool Inventory::HasKeyFlags(uint32_t wanted, const std::function<uint32_t(int64_t)>& keyFlagsOf) const {
    uint32_t held = 0;
    for (const InvItem& item : items_) {
        if (!item.IsProp()) {
            continue;
        }
        const uint32_t flags = keyFlagsOf(item.object);
        if (flags == 0) {
            continue;
        }
        held |= flags;
        if ((wanted & held) == wanted) {
            return true;
        }
    }
    return false;
}

void Inventory::AddTextOverride(const TextOverride& override) {
    overrides_.push_back(override);
}

const TextOverride* Inventory::TextByObject(int64_t object) const {
    // The newest is found first: bondinvAddTextOverride pushes on the head.
    for (auto it = overrides_.rbegin(); it != overrides_.rend(); ++it) {
        if (it->object == object) {
            return &*it;
        }
    }
    return nullptr;
}

const TextOverride* Inventory::TextByWeapon(int32_t weapon) const {
    for (auto it = overrides_.rbegin(); it != overrides_.rend(); ++it) {
        if (it->object < 0 && it->weapon == weapon) {
            return &*it;
        }
    }
    return nullptr;
}

int32_t Inventory::TextIndex(size_t index) const {
    if (index >= items_.size()) {
        // The all guns cheat's own entries: the weapon whose id is one more.
        return allGuns_ && index < static_cast<size_t>(kItemTankShells) ? static_cast<int32_t>(index) + 1 : kItemUnarmed;
    }
    const InvItem& item = items_[index];
    if (item.IsProp()) {
        const TextOverride* override = TextByObject(item.object);
        return override != nullptr ? override->weapon : kItemUnarmed;
    }
    if (item.IsDual()) {
        return kItemUnarmed;
    }
    return item.right;
}

} // namespace Frigate::Level
