#include <algorithm>

#include "level/Stage.h"

namespace Frigate::Level {

namespace {

namespace Cheat = Game::Menu::Cheat;

// LMISC's strings the cheats show as they switch on.
constexpr int kInvincibilityOn = 0x0F;
constexpr int kAllGunsOn = 0x10;
constexpr int kInvisibilityOn = 0x14;
constexpr int kInfiniteAmmoOn = 0x16;
constexpr int kDkModeOn = 0x17;
constexpr int kExtraWeapons = 0x18;
constexpr int kTinyBondOn = 0x19;
constexpr int kPaintballOn = 0x1A;
constexpr int kFastModeOn = 0x1D;
constexpr int kSlowMotion = 0x32;
constexpr int kFastMotion = 0x34;

// AMMO_TYPES.
constexpr int32_t kAmmo9mm = 1;
constexpr int32_t kAmmoRockets = 6;
constexpr int32_t kAmmoKnife = 10;
constexpr int32_t kAmmoGrenadeRound = 11;
constexpr int32_t kAmmoMagnum = 12;
constexpr int32_t kAmmoGoldenGun = 13;

} // namespace

void Stage::ShowMiscMessage(int slot) {
    hud_.BottomShow(lang_.Get(StringId(TextBank::Misc, slot)));
}

void Stage::TurnOnActivatedCheats(bool enemyRockets) {
    std::sort(activatedCheats_.begin(), activatedCheats_.end());
    for (const int32_t cheat : activatedCheats_) {
        if (cheat >= 1 && cheat < Cheat::kInvalid && (cheat == Cheat::kEnemyRockets) == enemyRockets) {
            TurnOnCheat(cheat);
        }
    }
}

// cheatButtonHandleCheatsTurnedOn, with one player.
// TODO(https://github.com/liz-baker/frigate/issues/337): the cheats only the
// in-stage button codes reach (extra multiplayer characters, maximum ammo, the
// health and armour boosts, line mode, bond phase, no radar and the debug
// ones) come with the codes themselves.
void Stage::TurnOnCheat(int32_t cheat) {
    cheats_.TurnOn(cheat);
    Inventory& inventory = intro_.inventory;
    // give_cur_player_ammo(type, get_max_ammo_for_type(type)).
    const auto fillAmmo = [this](int32_t ammoType) {
        if (weapons_.Ready()) {
            weapons_.GiveAmmo(ammoType, weapons_.Table().MaxAmmo(ammoType));
        }
    };
    // bondinvAddDoublesInvItem(item, item) and the ammo that goes with it.
    const auto giveDouble = [&](int32_t item, int32_t ammoType) {
        inventory.AddDoubles(item, item);
        if (ammoType >= 0) {
            fillAmmo(ammoType);
        }
    };

    switch (cheat) {
        case Cheat::kInvincibility:
            if (!cheatInvincible_) {
                ShowMiscMessage(kInvincibilityOn);
                cheatInvincible_ = true;
            }
            break;
        case Cheat::kAllGuns:
            if (!inventory.AllGuns()) {
                ShowMiscMessage(kAllGunsOn);
                inventory.SetAllGuns(true);
            }
            break;
        case Cheat::kInvisibility:
            if (visibleToGuards_) {
                ShowMiscMessage(kInvisibilityOn);
                visibleToGuards_ = false;
            }
            break;
        case Cheat::kInfiniteAmmo:
            // RenderEffects fills the ammo every frame.
            ShowMiscMessage(kInfiniteAmmoOn);
            break;
        case Cheat::kDkMode:
            ShowMiscMessage(kDkModeOn);
            chrs_.ScaleModels(0.8f);
            break;
        case Cheat::kExtraWeapons: {
            int added = inventory.AddItem(kItemRuger) ? 1 : 0;
            added += inventory.AddItem(kItemLaser) ? 1 : 0;
            added += inventory.AddItem(kItemGoldenGun) ? 1 : 0;
            added += inventory.AddItem(kItemSilverPp7) ? 1 : 0;
            added += inventory.AddItem(kItemGoldPp7) ? 1 : 0;
            fillAmmo(kAmmoMagnum);
            fillAmmo(kAmmoGoldenGun);
            fillAmmo(kAmmo9mm);
            if (added > 0) {
                ShowMiscMessage(kExtraWeapons);
            }
            break;
        }
        case Cheat::kTinyBond:
            if (stats_[0].perspectiveHeight == 1.0f) {
                ShowMiscMessage(kTinyBondOn);
                stats_[0].perspectiveHeight = 0.5f;
                // TODO(https://github.com/liz-baker/frigate/issues/337): the
                // player's own body model (bodyModel) is scaled to half as well.
            }
            break;
        case Cheat::kPaintball:
            ShowMiscMessage(kPaintballOn);
            break;
        case Cheat::kMagnum:
            inventory.AddItem(kItemRuger);
            fillAmmo(kAmmoMagnum);
            break;
        case Cheat::kLaser:
            inventory.AddItem(kItemLaser);
            break;
        case Cheat::kGoldenGun:
            inventory.AddItem(kItemGoldenGun);
            fillAmmo(kAmmoGoldenGun);
            break;
        case Cheat::kSilverPp7:
            inventory.AddItem(kItemSilverPp7);
            fillAmmo(kAmmo9mm);
            break;
        case Cheat::kGoldPp7:
            inventory.AddItem(kItemGoldPp7);
            fillAmmo(kAmmo9mm);
            break;
        case Cheat::kTurboMode:
            if (!fastBond_) {
                ShowMiscMessage(kFastModeOn);
                fastBond_ = true;
            }
            break;
        case Cheat::kFastAnimation:
            if (chrs_.AnimationRate() < 4.0f) {
                ShowMiscMessage(kFastMotion);
                chrs_.SetAnimationRate(4.0f);
            }
            break;
        case Cheat::kSlowAnimation:
            if (chrs_.AnimationRate() > 0.25f) {
                ShowMiscMessage(kSlowMotion);
                chrs_.SetAnimationRate(0.25f);
            }
            break;
        case Cheat::k2xRocketLauncher:
            giveDouble(kItemRocketLauncher, kAmmoRockets);
            break;
        case Cheat::k2xGrenadeLauncher:
            giveDouble(kItemGrenadeLauncher, kAmmoGrenadeRound);
            break;
        case Cheat::k2xRcp90:
            giveDouble(kItemRcp90, kAmmo9mm);
            break;
        case Cheat::k2xThrowingKnife:
            giveDouble(kItemThrowKnife, kAmmoKnife);
            break;
        case Cheat::k2xHuntingKnife:
            giveDouble(kItemKnife, -1);
            break;
        case Cheat::k2xLaser:
            giveDouble(kItemLaser, -1);
            break;
        default:
            break;
    }
}

} // namespace Frigate::Level
