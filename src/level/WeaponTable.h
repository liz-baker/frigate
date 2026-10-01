#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Level {

// ITEM_IDS values the gun code names (kItemUnarmed and kItemFist are in
// level/Inventory.h).
constexpr int32_t kItemKnife = 2;
constexpr int32_t kItemThrowKnife = 3;
constexpr int32_t kItemPp7 = 4;
constexpr int32_t kItemPp7Silenced = 5;
constexpr int32_t kItemDd44 = 6;
constexpr int32_t kItemKlobb = 7;
constexpr int32_t kItemKf7 = 8;
constexpr int32_t kItemZmg = 9;
constexpr int32_t kItemD5k = 10;
constexpr int32_t kItemD5kSilenced = 11;
constexpr int32_t kItemPhantom = 12;
constexpr int32_t kItemAr33 = 13;
constexpr int32_t kItemRcp90 = 14;
constexpr int32_t kItemShotgun = 15;
constexpr int32_t kItemAutoShotgun = 16;
constexpr int32_t kItemSniperRifle = 17;
constexpr int32_t kItemRuger = 18;
constexpr int32_t kItemGoldenGun = 19;
constexpr int32_t kItemSilverPp7 = 20;
constexpr int32_t kItemGoldPp7 = 21;
constexpr int32_t kItemLaser = 22;
constexpr int32_t kItemWatchLaser = 23;
constexpr int32_t kItemGrenadeLauncher = 24;
constexpr int32_t kItemRocketLauncher = 25;
constexpr int32_t kItemGrenade = 26;
constexpr int32_t kItemTimedMine = 27;
constexpr int32_t kItemProximityMine = 28;
constexpr int32_t kItemRemoteMine = 29;
constexpr int32_t kItemTrigger = 30;
constexpr int32_t kItemTaser = 31;
constexpr int32_t kItemTankShells = 32;
constexpr int32_t kItemBombCase = 33;
constexpr int32_t kItemPlastique = 34;
constexpr int32_t kItemFlarePistol = 35;
constexpr int32_t kItemPitonGun = 36;
constexpr int32_t kItemBungee = 37;
constexpr int32_t kItemDoorDecoder = 38;
constexpr int32_t kItemBombDefuser = 39;
constexpr int32_t kItemCamera = 40;
constexpr int32_t kItemLockExploder = 41;
constexpr int32_t kItemDoorExploder = 42;
constexpr int32_t kItemBriefcase = 43;
constexpr int32_t kItemWeaponCase = 44;
constexpr int32_t kItemSafeCrackerCase = 45;
constexpr int32_t kItemKeyAnalyserCase = 46;
constexpr int32_t kItemBug = 47;
constexpr int32_t kItemMicroCamera = 48;
constexpr int32_t kItemBugDetector = 49;
constexpr int32_t kItemExplosiveFloppy = 50;
constexpr int32_t kItemPolarizedGlasses = 51;
constexpr int32_t kItemDarkGlasses = 52;
constexpr int32_t kItemCreditCard = 53;
constexpr int32_t kItemGasKeyRing = 54;
constexpr int32_t kItemDataThief = 55;
constexpr int32_t kItemWatchIdentifier = 56;
constexpr int32_t kItemWatchCommunicator = 57;
constexpr int32_t kItemWatchGeigerCounter = 58;
constexpr int32_t kItemWatchMagnetRepel = 59;
constexpr int32_t kItemWatchMagnetAttract = 60;
constexpr int32_t kItemGoldenEyeKey = 61;
constexpr int32_t kItemDatTape = 73;
constexpr int32_t kItemKeyCard = 81;
constexpr int32_t kItemKeyYale = 82;
constexpr int32_t kItemKeyBolt = 83;
constexpr int32_t kItemSuitLeftHand = 84;
constexpr int32_t kItemJoypad = 85;
constexpr int32_t kItemRocketRound = 86;
constexpr int32_t kItemGrenadeRound = 87;
constexpr int32_t kItemToken = 88;


// The WEAPONSTATBITFLAG_* bits the level code tests.
namespace WeaponFlag {
// WEAPONSTATBITFLAG_00000001: the muzzle flash is turned by a random angle.
constexpr uint32_t RotatedFlash = 0x1;
constexpr uint32_t SingleUseReload = 0x2;
constexpr uint32_t BurstFire = 0x4;
constexpr uint32_t HasAutoAim = 0x8;
constexpr uint32_t Clicky = 0x10;
// WEAPONSTATBITFLAG_00000020 and _00000040: the gun shakes while firing, and
// the second one shakes it sideways too.
constexpr uint32_t FiringShake = 0x20;
constexpr uint32_t FiringShakeSideways = 0x40;
// WEAPONSTATBITFLAG_DUAL_WIELD_ALTERNATING_FIRE.
constexpr uint32_t DualWieldAlternating = 0x80;
// WEAPONSTATBITFLAG_ONLY_1_HANDED (with HoldAsGun) and _HOLD_AS_GUN.
constexpr uint32_t OnlyOneHanded = 0x100;
constexpr uint32_t HoldAsGun = 0x200;
constexpr uint32_t MirrorDual = 0x400;
constexpr uint32_t ShowFirstPerson = 0x800;
constexpr uint32_t HideFirstPersonHand = 0x2000;
constexpr uint32_t DisableCrouch = 0x8000;
constexpr uint32_t UseHoldTime = 0x20000;
constexpr uint32_t HasAmmo = 0x40000;
constexpr uint32_t HideAmmoDisplay = 0x80000;
constexpr uint32_t CanDualWield = 0x100000;
constexpr uint32_t AmmoClipLimit = 0x200000;
constexpr uint32_t NoClipReloads = 0x400000;
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
    // The resource base path of the model the item holds; empty for a record
    // with no model file.
    std::string model;
    // The model header's skeleton name; empty when it has none.
    std::string skeleton;
    // MuzzleFlashExtension.
    float muzzleFlashExtension = 0.0f;
    // PosX, PosY, PosZ: where the gun sits on screen.
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    // PlayX, PlayY, PlayZ: how far the gun follows the crosshair.
    float playX = 0.0f;
    float playY = 0.0f;
    float playZ = 0.0f;
    // CrosshairSpeed and AimLockSpeed.
    float crosshairSpeed = 0.0f;
    float aimLockSpeed = 0.0f;
    // Sway.
    float sway = 0.0f;
    // BoltRecoilBack.
    float boltRecoilBack = 0.0f;
    // AutomaticFiringRate and SingleFiringRate (signed bytes).
    int32_t automaticFiringRate = 0;
    int32_t singleFiringRate = 0;
    // RecoilSpeed's b44[0] to b44[3].
    std::array<int32_t, 4> recoilSpeed{};
    // RecoilBack and RecoilUp.
    float recoilBack = 0.0f;
    float recoilUp = 0.0f;
    // The resource base path of the casing model the gun ejects; empty for
    // none (ptr_cartridge_struct).
    std::string cartridge;
    // LoudnessMin, LoudnessMax, NoiseIncreasePerShot, NoiseDecayLinearTime and
    // NoiseDecayScaledTime.
    float loudnessMin = 0.0f;
    float loudnessMax = 0.0f;
    float noiseIncreasePerShot = 0.0f;
    float noiseDecayLinearTime = 0.0f;
    float noiseDecayScaledTime = 0.0f;
    // Zoom.
    float zoom = 0.0f;
};

// One ammo type's row of ammo_related, less the carry limit.
struct AmmoIcon {
    // IconImage: the icon's entry in the global image table (its offset there),
    // 0 for no icon. The HUD draws it from the `hud/img/<offset>` list.
    int32_t image = 0;
    // IconYOffset.
    float yOffset = 0.0f;
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
    // ammo_related's IconImage and IconYOffset; no icon for a type outside the table.
    AmmoIcon Icon(int32_t ammoType) const;
    // The number of ammo types the table has.
    size_t AmmoTypeCount() const {
        return maxAmmo_.size();
    }

  private:
    std::vector<ItemStats> items_;
    std::vector<int32_t> maxAmmo_;
    std::vector<AmmoIcon> icons_;
};

} // namespace Frigate::Level
