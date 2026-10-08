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
    Cur().hud.BottomShow(lang_.Get(StringId(TextBank::Misc, slot)));
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
    for (const int32_t cheat : Cur().cheatInput.Feed(input.buttons, input.pressed)) {
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
    Inventory& inventory = Cur().intro.inventory;
    // give_cur_player_ammo(type, get_max_ammo_for_type(type)).
    const auto fillAmmo = [this](int32_t ammoType) {
        if (Cur().weapons.Ready()) {
            Cur().weapons.GiveAmmo(ammoType, Cur().weapons.Table().MaxAmmo(ammoType));
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
            if (Cur().weapons.Ready()) {
                Cur().weapons.SetMaxAmmo();
            }
            break;
        case Cheat::kLineMode:
            lineMode_ = true;
            break;
        case Cheat::k2xHealth:
            if (Cur().health.ActualHealth() == 1.0f || Cur().health.Health() < 1.0f) {
                ShowMiscMessage(kSuper2xHealth);
                Cur().health.SetHealth(1.0f);
                Cur().health.SetActualHealth(2.0f);
            }
            break;
        case Cheat::k2xArmor:
            if (Cur().health.ActualArmour() == 1.0f || Cur().health.Armour() < 1.0f) {
                ShowMiscMessage(kSuper2xArmour);
                Cur().health.SetArmour(1.0f);
                Cur().health.SetActualArmour(2.0f);
            }
            break;
        case Cheat::k10xHealth:
            if (Cur().health.ActualHealth() == 1.0f || Cur().health.Health() < 1.0f) {
                ShowMiscMessage(kSuper10xHealth);
                Cur().health.SetHealth(1.0f);
                Cur().health.SetActualHealth(10.0f);
            }
            break;
        case Cheat::kBondPhase:
            StartBodyFade(300.0f, 0.05f);
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
            if (CurStats().perspectiveHeight == 1.0f) {
                ShowMiscMessage(kTinyBondOn);
                CurStats().perspectiveHeight = 0.5f;
                if (Cur().bondChr >= 0) {
                    chrs_[Cur().bondChr].model->Playback().scale *= 0.5f;
                }
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
            if (Cur().intro.inventory.AllGuns()) {
                ShowMiscMessage(kAllGunsOff);
                Cur().intro.inventory.SetAllGuns(false);
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
            if (CurStats().perspectiveHeight != 1.0f) {
                ShowMiscMessage(kTinyBondOff);
                CurStats().perspectiveHeight = 1.0f;
                if (Cur().bondChr >= 0) {
                    chrs_[Cur().bondChr].model->Playback().scale *= 2.0f;
                }
            }
            break;
        case Cheat::kPaintball:
            ShowMiscMessage(kPaintballOff);
            break;
        case Cheat::kBondPhase:
            StartBodyFade(300.0f, 1.0f);
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
