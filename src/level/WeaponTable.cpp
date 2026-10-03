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
        if (fields[0] == "item" && fields.size() == 40) {
            size_t index = 0;
            ItemStats stats;
            int hasHeader = 0;
            float shootThrough = 0.0f;
            if (!ParseNumber(fields[1], index) || index != table->items_.size() || !ParseNumber(fields[3], hasHeader) ||
                !ParseNumber(fields[4], stats.ammoType) || !ParseNumber(fields[5], stats.magSize) ||
                !ParseNumber(fields[6], stats.flags, 16) || !ParseFloat(fields[9], stats.muzzleFlashExtension) ||
                !ParseFloat(fields[10], stats.posX) || !ParseFloat(fields[11], stats.posY) ||
                !ParseFloat(fields[12], stats.posZ) || !ParseFloat(fields[13], stats.playX) ||
                !ParseFloat(fields[14], stats.playY) || !ParseFloat(fields[15], stats.playZ) ||
                !ParseFloat(fields[16], stats.crosshairSpeed) || !ParseFloat(fields[17], stats.aimLockSpeed) ||
                !ParseFloat(fields[18], stats.sway) || !ParseFloat(fields[19], stats.boltRecoilBack) ||
                !ParseNumber(fields[20], stats.automaticFiringRate) ||
                !ParseNumber(fields[21], stats.singleFiringRate) || !ParseNumber(fields[22], stats.recoilSpeed[0]) ||
                !ParseNumber(fields[23], stats.recoilSpeed[1]) || !ParseNumber(fields[24], stats.recoilSpeed[2]) ||
                !ParseNumber(fields[25], stats.recoilSpeed[3]) || !ParseFloat(fields[26], stats.recoilBack) ||
                !ParseFloat(fields[27], stats.recoilUp) || !ParseFloat(fields[29], stats.loudnessMin) ||
                !ParseFloat(fields[30], stats.loudnessMax) || !ParseFloat(fields[31], stats.noiseIncreasePerShot) ||
                !ParseFloat(fields[32], stats.noiseDecayLinearTime) ||
                !ParseFloat(fields[33], stats.noiseDecayScaledTime) || !ParseFloat(fields[34], stats.zoom) ||
                !ParseFloat(fields[35], stats.inaccuracy) || !ParseFloat(fields[36], shootThrough) ||
                !ParseFloat(fields[37], stats.destructionAmount) || !ParseNumber(fields[38], stats.soundTriggerRate) ||
                !ParseNumber(fields[39], stats.sound)) {
                return nullptr;
            }
            stats.objectsShootThrough = static_cast<int32_t>(shootThrough);
            stats.cartridge = fields[28] == "-" ? "" : std::string(fields[28]);
            stats.hasHeader = hasHeader != 0;
            stats.model = fields[7] == "-" ? "" : std::string(fields[7]);
            stats.skeleton = fields[8] == "-" ? "" : std::string(fields[8]);
            table->items_.push_back(stats);
        } else if (fields[0] == "watch" && fields.size() == 14) {
            size_t index = 0;
            WatchItem watch;
            if (!ParseNumber(fields[1], index) || index != table->watch_.size() ||
                !ParseNumber(fields[2], watch.upperText, 16) || !ParseNumber(fields[3], watch.lowerText, 16) ||
                !ParseFloat(fields[4], watch.statusX) || !ParseFloat(fields[5], watch.statusY) ||
                !ParseFloat(fields[6], watch.statusZ) || !ParseFloat(fields[7], watch.rotX) ||
                !ParseFloat(fields[8], watch.rotY) || !ParseNumber(fields[9], watch.longName, 16) ||
                !ParseNumber(fields[10], watch.shortName, 16) || !ParseFloat(fields[11], watch.listX) ||
                !ParseFloat(fields[12], watch.listY) || !ParseFloat(fields[13], watch.listZ)) {
                return nullptr;
            }
            table->watch_.push_back(watch);
        } else if (fields[0] == "ammo" && fields.size() == 6) {
            size_t index = 0;
            int32_t limit = 0;
            AmmoIcon icon;
            if (!ParseNumber(fields[1], index) || index != table->maxAmmo_.size() || !ParseNumber(fields[3], limit) ||
                !ParseNumber(fields[4], icon.image, 16) || !ParseFloat(fields[5], icon.yOffset)) {
                return nullptr;
            }
            table->maxAmmo_.push_back(limit);
            table->icons_.push_back(icon);
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

const WatchItem& WeaponTable::Watch(int32_t item) const {
    static const WatchItem kNone;
    if (watch_.empty()) {
        return kNone;
    }
    if (item < 0 || static_cast<size_t>(item) >= watch_.size()) {
        return watch_.front();
    }
    return watch_[static_cast<size_t>(item)];
}

int32_t WeaponTable::MaxAmmo(int32_t ammoType) const {
    if (ammoType < 0 || static_cast<size_t>(ammoType) >= maxAmmo_.size()) {
        return 0;
    }
    return maxAmmo_[static_cast<size_t>(ammoType)];
}

AmmoIcon WeaponTable::Icon(int32_t ammoType) const {
    if (ammoType < 0 || static_cast<size_t>(ammoType) >= icons_.size()) {
        return {};
    }
    return icons_[static_cast<size_t>(ammoType)];
}

} // namespace Frigate::Level
