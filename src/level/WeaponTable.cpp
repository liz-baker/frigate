#include "level/WeaponTable.h"

#include <charconv>
#include <string>

namespace Frigate::Level {

namespace {

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

template <typename T> bool ParseNumber(std::string_view text, T& out, int base = 10) {
    if (base == 16 && text.size() > 2 && text[0] == '0' && text[1] == 'x') {
        text.remove_prefix(2);
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return error == std::errc() && end == text.data() + text.size();
}

} // namespace

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
