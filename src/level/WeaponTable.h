#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace Frigate::Level {

// The WEAPONSTATBITFLAG_* bits the level code tests.
namespace WeaponFlag {
constexpr uint32_t Clicky = 0x10;
constexpr uint32_t HasAutoAim = 0x8;
constexpr uint32_t ShowFirstPerson = 0x800;
constexpr uint32_t HideFirstPersonHand = 0x2000;
constexpr uint32_t DisableCrouch = 0x8000;
constexpr uint32_t CanDualWield = 0x100000;
constexpr uint32_t AmmoClipLimit = 0x200000;
} // namespace WeaponFlag

// The fields of one item's WeaponStats that the level code reads
// (get_ptr_item_statistics).
struct ItemStats {
    // The item's record has a model header.
    bool hasHeader = false;
    // AmmoType.
    int32_t ammoType = 0;
    // MagSize.
    int32_t magSize = 0;
    // BitFlags.
    uint32_t flags = 0;
};

// The weapon and ammo tables, from the `weapon/stats` resource
// (docs/formats/goldeneye-weapon-stats.md).
class WeaponTable {
  public:
    // Parses the resource's text. Returns nullptr if it is malformed.
    static std::shared_ptr<const WeaponTable> Parse(std::string_view text);

    // get_ptr_item_statistics; the defaults for an item outside the table.
    const ItemStats& Item(int32_t item) const;
    // bondwalkItemCheckBitflags.
    bool HasFlag(int32_t item, uint32_t mask) const {
        return (Item(item).flags & mask) != 0;
    }
    // get_ammo_type_for_weapon.
    int32_t AmmoType(int32_t item) const {
        return Item(item).ammoType;
    }
    // get_max_ammo_for_type (ammo_related's MaxAmmo).
    int32_t MaxAmmo(int32_t ammoType) const;
    // The number of ammo types the table has.
    size_t AmmoTypeCount() const {
        return maxAmmo_.size();
    }

  private:
    std::vector<ItemStats> items_;
    std::vector<int32_t> maxAmmo_;
};

} // namespace Frigate::Level
