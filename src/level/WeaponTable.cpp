#include "level/WeaponTable.h"

#include <string>

#include "level/TextTable.h"

namespace Frigate::Level {

std::shared_ptr<const WeaponTable> WeaponTable::Parse(std::string_view text) {
    std::shared_ptr<WeaponTable> table(new WeaponTable());
    for (std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        const auto fields = Split(line, '\t');
        if (fields[0] == "item" && fields.size() == 7) {
            size_t index = 0;
            ItemStats stats;
            int hasHeader = 0;
            if (!ParseNumber(fields[1], index) || index != table->items_.size() || !ParseNumber(fields[3], hasHeader) ||
                !ParseNumber(fields[4], stats.ammoType) || !ParseNumber(fields[5], stats.magSize) ||
                !ParseNumber(fields[6], stats.flags, 16)) {
                return nullptr;
            }
            stats.hasHeader = hasHeader != 0;
            table->items_.push_back(stats);
        } else if (fields[0] == "ammo" && fields.size() == 4) {
            size_t index = 0;
            int32_t limit = 0;
            if (!ParseNumber(fields[1], index) || index != table->maxAmmo_.size() || !ParseNumber(fields[3], limit)) {
                return nullptr;
            }
            table->maxAmmo_.push_back(limit);
        } else {
            return nullptr;
        }
    }
    if (table->items_.empty() || table->maxAmmo_.empty()) {
        return nullptr;
    }
    return table;
}

const ItemStats& WeaponTable::Item(int32_t item) const {
    // Item 0, ITEM_UNARMED, has no model and so carries the defaults.
    if (item < 0 || static_cast<size_t>(item) >= items_.size()) {
        return items_.front();
    }
    return items_[static_cast<size_t>(item)];
}

int32_t WeaponTable::MaxAmmo(int32_t ammoType) const {
    if (ammoType < 0 || static_cast<size_t>(ammoType) >= maxAmmo_.size()) {
        return 0;
    }
    return maxAmmo_[static_cast<size_t>(ammoType)];
}

} // namespace Frigate::Level
