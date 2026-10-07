#include "level/MpWeaponSets.h"

#include "level/Inventory.h"
#include "level/PickupText.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

constexpr int kDefaultSet = 0xB;

constexpr MpWeaponSetEntry Row(int32_t item, int32_t prop, float size, int32_t ammoType, int32_t ammoAmount,
                               bool allowPickup) {
    return { item, prop, size, ammoType, ammoAmount, allowPickup };
}

// Most sets give each weapon two slots in a row.
constexpr MpWeaponSet Pairs(const MpWeaponSetEntry& a, const MpWeaponSetEntry& b, const MpWeaponSetEntry& c,
                            const MpWeaponSetEntry& d) {
    return { a, a, b, b, c, c, d, d };
}

constexpr MpWeaponSetEntry kSlap = Row(kItemUnarmed, MpProp::ChrTt33, 1.0f, AmmoKind::Ammo9mm, 0, true);
constexpr MpWeaponSetEntry kDd44 = Row(kItemDd44, MpProp::ChrTt33, 1.0f, AmmoKind::Ammo9mm, 0x32, true);
constexpr MpWeaponSetEntry kDd44Big = Row(kItemDd44, MpProp::ChrTt33, 3.0f, AmmoKind::Ammo9mm, 0x32, true);
constexpr MpWeaponSetEntry kPp7Silenced =
    Row(kItemPp7Silenced, MpProp::ChrWppkSil, 1.0f, AmmoKind::Ammo9mm, 0x32, true);
constexpr MpWeaponSetEntry kPp7Big = Row(kItemPp7, MpProp::ChrWppk, 3.0f, AmmoKind::Ammo9mm, 0x32, true);
constexpr MpWeaponSetEntry kCougar = Row(kItemRuger, MpProp::ChrRuger, 1.0f, AmmoKind::Magnum, 0x32, true);
constexpr MpWeaponSetEntry kKlobb = Row(kItemKlobb, MpProp::ChrSkorpion, 1.5f, AmmoKind::Ammo9mm, 0x64, true);
constexpr MpWeaponSetEntry kKlobbSmall = Row(kItemKlobb, MpProp::ChrSkorpion, 1.0f, AmmoKind::Ammo9mm, 0x64, true);
constexpr MpWeaponSetEntry kKf7 = Row(kItemKf7, MpProp::ChrKalash, 1.5f, AmmoKind::Rifle, 0x64, true);
constexpr MpWeaponSetEntry kZmg = Row(kItemZmg, MpProp::ChrUzi, 1.5f, AmmoKind::Ammo9mm, 0x64, true);
constexpr MpWeaponSetEntry kThrowingKnife =
    Row(kItemThrowKnife, MpProp::ChrThrowKnife, 1.0f, AmmoKind::Knife, 0xA, false);

} // namespace

const MpWeaponSet& MpWeaponSetData(int set) {
    // mp_weapon_set_slaps to mp_weapon_set_golden. The remote and timed mine
    // sets show the AR33 as the KF7 and the proximity set as the AR33: the
    // table of the US build the GoldenEye decomp matches.
    static const std::array<MpWeaponSet, kMpWeaponSetCount> kSets = {
        MpWeaponSet{ kSlap, kSlap, kSlap, kSlap, kSlap, kSlap, kSlap, kSlap },
        MpWeaponSet{ kDd44, kDd44, kDd44, kPp7Silenced, kPp7Silenced, kPp7Silenced, kCougar, kCougar },
        Pairs(kThrowingKnife, kThrowingKnife, kThrowingKnife, kThrowingKnife),
        Pairs(kPp7Silenced, kDd44, kKlobb, Row(kItemD5k, MpProp::ChrMp5k, 1.0f, AmmoKind::Ammo9mm, 0x64, true)),
        Pairs(kDd44Big, kCougar, Row(kItemRcp90, MpProp::ChrFnp90, 1.0f, AmmoKind::Ammo9mm, 0x64, true),
              Row(kItemAutoShotgun, MpProp::ChrAutoShot, 1.0f, AmmoKind::Shotgun, 0x1E, true)),
        Pairs(kDd44Big, kCougar, kKlobbSmall,
              Row(kItemSniperRifle, MpProp::ChrSniperRifle, 1.0f, AmmoKind::Rifle, 0x32, true)),
        Pairs(kDd44Big, kKlobb, kKf7, Row(kItemGrenade, MpProp::ChrGrenade, 1.5f, AmmoKind::Grenade, 5, false)),
        Pairs(kPp7Big, kZmg, Row(kItemAr33, MpProp::ChrKalash, 1.5f, AmmoKind::Rifle, 0x64, true),
              Row(kItemRemoteMine, MpProp::ChrRemoteMine, 1.5f, AmmoKind::RemoteMine, 5, false)),
        Pairs(kDd44Big, kKlobb, kKf7,
              Row(kItemGrenadeLauncher, MpProp::ChrGrenadeLaunch, 1.0f, AmmoKind::GrenadeRound, 6, true)),
        Pairs(kPp7Big, kZmg, Row(kItemAr33, MpProp::ChrKalash, 1.5f, AmmoKind::Rifle, 0x64, true),
              Row(kItemTimedMine, MpProp::ChrTimedMine, 1.5f, AmmoKind::TimedMine, 5, false)),
        Pairs(kPp7Big, kZmg, Row(kItemAr33, MpProp::ChrM16, 1.5f, AmmoKind::Rifle, 0x64, true),
              Row(kItemProximityMine, MpProp::ChrProximityMine, 1.5f, AmmoKind::ProximityMine, 5, false)),
        Pairs(kDd44Big, kKlobb, kKf7,
              Row(kItemRocketLauncher, MpProp::ChrRocketLaunch, 1.5f, AmmoKind::Rockets, 6, true)),
        Pairs(kDd44Big, kKlobb, kKf7, Row(kItemLaser, MpProp::ChrLaser, 1.5f, 0, 0, true)),
        MpWeaponSet{ kDd44Big, kDd44Big, kKlobb, kKlobb, kKf7, kKf7, kPp7Silenced,
                     Row(kItemGoldenGun, MpProp::ChrGolden, 1.5f, AmmoKind::GoldenGun, 0xA, true) },
    };
    if (set < 0 || static_cast<size_t>(set) >= kSets.size()) {
        set = kDefaultSet;
    }
    return kSets[static_cast<size_t>(set)];
}

bool MpWeaponStocking::StockWeapon(PropDef* def) {
    constexpr int32_t kSlotBase = 0xF0;
    lastSlot_ = -1;
    bool give = true;
    const auto code = static_cast<uint8_t>(def->weaponNum);
    if (code >= kSlotBase && code < kSlotBase + static_cast<int32_t>(kMpWeaponSetSlots)) {
        const int slot = code - kSlotBase;
        const MpWeaponSetEntry& entry = MpWeaponSetData(set_)[static_cast<size_t>(slot)];
        lastSlot_ = slot;
        def->weaponNum = entry.item;
        def->model = static_cast<int16_t>(entry.prop);
        def->extraScale = static_cast<uint16_t>(entry.size * 256.0f);
        give = entry.allowPickup;
    } else if (code == kItemToken) {
        give = tokenScenario_;
    }
    return def->weaponNum != kItemUnarmed && give;
}

bool MpWeaponStocking::StockAmmoCrate(PropDef* def) {
    // The decomp indexes the set with lastmpweaponnum unchecked; no stage puts a
    // crate after a weapon that is not a set slot, so there is nothing to
    // stock one with.
    if (lastSlot_ < 0) {
        return true;
    }
    const MpWeaponSetEntry& entry = MpWeaponSetData(set_)[static_cast<size_t>(lastSlot_)];
    // A set with no ammunition (the lasers) writes no slot, and has no crate.
    if (entry.ammoType > 0) {
        def->ammoSlots[static_cast<size_t>(entry.ammoType - 1)].quantity = static_cast<uint16_t>(entry.ammoAmount);
    }
    return entry.ammoAmount > 0;
}

} // namespace Frigate::Level
