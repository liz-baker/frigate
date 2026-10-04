#include <algorithm>

#include "level/Stage.h"

namespace Frigate::Level {

namespace {

namespace Cheat = Game::Menu::Cheat;

// LMISC's strings the cheats show as they switch on.
constexpr int kInvincibilityOn = 0x0F;
constexpr int kAllGunsOn = 0x10;
constexpr int kMaximumAmmo = 0x11;
constexpr int kSuper2xHealth = 0x12;
constexpr int kSuper2xArmour = 0x13;
constexpr int kInvisibilityOn = 0x14;
constexpr int kInfiniteAmmoOn = 0x16;
constexpr int kDkModeOn = 0x17;
constexpr int kExtraWeapons = 0x18;
constexpr int kTinyBondOn = 0x19;
constexpr int kPaintballOn = 0x1A;
constexpr int kSuper10xHealth = 0x1B;
constexpr int kFastModeOn = 0x1D;
constexpr int kInvincibilityOff = 0x1E;
constexpr int kAllGunsOff = 0x1F;
constexpr int kInvisibilityOff = 0x20;
constexpr int kInfiniteAmmoOff = 0x22;
constexpr int kDkModeOff = 0x23;
constexpr int kTinyBondOff = 0x24;
constexpr int kPaintballOff = 0x25;
constexpr int kFastModeOff = 0x27;
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

void Stage::TickCheatInput(const MoveInput& input) {
    for (const int32_t cheat : cheatInput_.Feed(input.buttons, input.pressed)) {
        if (!cheats_.IsActive(cheat) || CheatShowsText(cheat)) {
            TurnOnCheat(cheat);
        } else {
            TurnOffCheat(cheat);
        }
    }
}

// cheatButtonHandleCheatsTurnedOn, with one player. The extra multiplayer
// characters (CHEAT_EXTRA_MP_CHARS) are unlocked from the menus' codes, never
// in a stage; CHEAT_NO_RADAR_MP, CHEAT_BONDPHASE's player count and
// CHEAT_DEACTIVATE_INVINCIBILITY only matter to multiplayer games, and
// CHEAT_DEBUG_POS's flag has no reader in the retail game.
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
        case Cheat::kMaxAmmo:
            ShowMiscMessage(kMaximumAmmo);
            if (weapons_.Ready()) {
                weapons_.SetMaxAmmo();
            }
            break;
        case Cheat::kLineMode:
            lineMode_ = true;
            break;
        case Cheat::k2xHealth:
            if (health_.ActualHealth() == 1.0f || health_.Health() < 1.0f) {
                ShowMiscMessage(kSuper2xHealth);
                health_.SetHealth(1.0f);
                health_.SetActualHealth(2.0f);
            }
            break;
        case Cheat::k2xArmor:
            if (health_.ActualArmour() == 1.0f || health_.Armour() < 1.0f) {
                ShowMiscMessage(kSuper2xArmour);
                health_.SetArmour(1.0f);
                health_.SetActualArmour(2.0f);
            }
            break;
        case Cheat::k10xHealth:
            if (health_.ActualHealth() == 1.0f || health_.Health() < 1.0f) {
                ShowMiscMessage(kSuper10xHealth);
                health_.SetHealth(1.0f);
                health_.SetActualHealth(10.0f);
            }
            break;
        case Cheat::kBondPhase:
            colour_.StartBodyFade(300.0f, 0.05f, bodyFadeAlpha_);
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
                // TODO(https://github.com/liz-baker/frigate/issues/140): the
                // player's body model (bodyModel), which exists only while the
                // intro camera shows, is scaled to half as well.
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

// cheatButtonHandleCheatsTurnedOff, with one player.
void Stage::TurnOffCheat(int32_t cheat) {
    cheats_.TurnOff(cheat);
    switch (cheat) {
        case Cheat::kInvincibility:
            if (cheatInvincible_) {
                ShowMiscMessage(kInvincibilityOff);
                cheatInvincible_ = false;
            }
            break;
        case Cheat::kAllGuns:
            if (intro_.inventory.AllGuns()) {
                ShowMiscMessage(kAllGunsOff);
                intro_.inventory.SetAllGuns(false);
            }
            break;
        case Cheat::kLineMode:
            lineMode_ = false;
            break;
        case Cheat::kInvisibility:
            if (!visibleToGuards_) {
                ShowMiscMessage(kInvisibilityOff);
                visibleToGuards_ = true;
            }
            break;
        case Cheat::kInfiniteAmmo:
            ShowMiscMessage(kInfiniteAmmoOff);
            break;
        case Cheat::kDkMode:
            ShowMiscMessage(kDkModeOff);
            chrs_.ScaleModels(1.25f);
            break;
        case Cheat::kTinyBond:
            if (stats_[0].perspectiveHeight != 1.0f) {
                ShowMiscMessage(kTinyBondOff);
                stats_[0].perspectiveHeight = 1.0f;
                // TODO(https://github.com/liz-baker/frigate/issues/140): the
                // player's body model is scaled back up as well.
            }
            break;
        case Cheat::kPaintball:
            ShowMiscMessage(kPaintballOff);
            break;
        case Cheat::kBondPhase:
            colour_.StartBodyFade(300.0f, 1.0f, bodyFadeAlpha_);
            break;
        case Cheat::kTurboMode:
            if (fastBond_) {
                ShowMiscMessage(kFastModeOff);
                fastBond_ = false;
            }
            break;
        default:
            break;
    }
}

} // namespace Frigate::Level
