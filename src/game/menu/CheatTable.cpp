#include "game/menu/CheatTable.h"

#include <array>

#include "game/FrontState.h"
#include "level/CheatMasks.h"

namespace Frigate::Game::Menu::Cheat {

namespace {

constexpr int32_t kNoText = -1;

// g_CheatInfo's text_bank_id, from CHEAT_EXTRA_MP_CHARS.
constexpr std::array<int32_t, kCheatInvalid - 1> kEntries = { {
    kNoText, // CHEAT_EXTRA_MP_CHARS
    0x00,    // CHEAT_INVINCIBILITY
    0x01,    // CHEAT_ALLGUNS
    kNoText, // CHEAT_MAXAMMO
    kNoText, // CHEAT_DEBUG_UNK5
    kNoText, // CHEAT_DEACTIVATE_INVINCIBILITY
    0x37,    // CHEAT_LINEMODE
    kNoText, // CHEAT_2X_HEALTH
    kNoText, // CHEAT_2X_ARMOR
    0x04,    // CHEAT_INVISIBILITY
    0x06,    // CHEAT_INFINITE_AMMO
    0x07,    // CHEAT_DK_MODE
    kNoText, // CHEAT_EXTRA_WEAPONS
    0x08,    // CHEAT_TINY_BOND
    0x38,    // CHEAT_PAINTBALL
    kNoText, // CHEAT_10X_HEALTH
    0x0A,    // CHEAT_MAGNUM
    0x0B,    // CHEAT_LASER
    0x0C,    // CHEAT_GOLDEN_GUN
    0x0D,    // CHEAT_SILVER_PP7
    0x0E,    // CHEAT_GOLD_PP7
    kNoText, // CHEAT_BONDPHASE
    0x43,    // CHEAT_NO_RADAR_MP
    0x40,    // CHEAT_TURBO_MODE
    kNoText, // CHEAT_DEBUG_POS
    0x41,    // CHEAT_FAST_ANIMATION
    0x42,    // CHEAT_SLOW_ANIMATION
    0x39,    // CHEAT_ENEMY_ROCKETS
    0x3A,    // CHEAT_2X_ROCKET_LAUNCHER
    0x3B,    // CHEAT_2X_GRENADE_LAUNCHER
    0x3C,    // CHEAT_2X_RCP90
    0x3D,    // CHEAT_2X_THROWING_KNIFE
    0x3E,    // CHEAT_2X_HUNTING_KNIFE
    0x3F,    // CHEAT_2X_LASER
    kNoText, // CHEAT_UNLOCK_PAINTBALL
    kNoText, // CHEAT_UNLOCK_INVINCIBLE
    kNoText, // CHEAT_UNLOCK_DKMODE
    kNoText, // CHEAT_UNLOCK_2XGL
    kNoText, // CHEAT_UNLOCK_2XRL
    kNoText, // CHEAT_UNLOCK_TURBO
    kNoText, // CHEAT_UNLOCK_NORADAR
    kNoText, // CHEAT_UNLOCK_TINY
    kNoText, // CHEAT_UNLOCK_2XTKNIFE
    kNoText, // CHEAT_UNLOCK_FAST
    kNoText, // CHEAT_UNLOCK_INVIS
    kNoText, // CHEAT_UNLOCK_ENEMYRL
    kNoText, // CHEAT_UNLOCK_SLOW
    kNoText, // CHEAT_UNLOCK_SILVERPPK
    kNoText, // CHEAT_UNLOCK_2XHKNIFE
    kNoText, // CHEAT_UNLOCK_INFAMMO
    kNoText, // CHEAT_UNLOCK_2XFNP0
    kNoText, // CHEAT_UNLOCK_GOLDPPK
    kNoText, // CHEAT_UNLOCK_2XLASER
    kNoText, // CHEAT_UNLOCK_ALLGUNS
    kNoText, // CHEAT_UNLOCK_DAM
    kNoText, // CHEAT_UNLOCK_FACILITY
    kNoText, // CHEAT_UNLOCK_RUNWAY
    kNoText, // CHEAT_UNLOCK_SURFACE
    kNoText, // CHEAT_UNLOCK_BUNKER
    kNoText, // CHEAT_UNLOCK_SILO
    kNoText, // CHEAT_UNLOCK_FRIGATE
    kNoText, // CHEAT_UNLOCK_SURFACE2
    kNoText, // CHEAT_UNLOCK_BUNKER2
    kNoText, // CHEAT_UNLOCK_STATUE
    kNoText, // CHEAT_UNLOCK_ARCHIVES
    kNoText, // CHEAT_UNLOCK_STREETS
    kNoText, // CHEAT_UNLOCK_DEPOT
    kNoText, // CHEAT_UNLOCK_TRAIN
    kNoText, // CHEAT_UNLOCK_JUNGLE
    kNoText, // CHEAT_UNLOCK_CONTROL
    kNoText, // CHEAT_UNLOCK_CAVERNS
    kNoText, // CHEAT_UNLOCK_CRADLE
    kNoText, // CHEAT_UNLOCK_AZTEK
    kNoText, // CHEAT_UNLOCK_EGYPT
} };

} // namespace

std::optional<int32_t> MenuTextSlot(int32_t cheat) {
    if (cheat < 1 || cheat >= static_cast<int32_t>(kCheatInvalid)) {
        return std::nullopt;
    }
    const int32_t slot = kEntries[static_cast<size_t>(cheat - 1)];
    return slot == kNoText ? std::nullopt : std::optional<int32_t>(slot);
}

bool IsMultiplayerCheat(int32_t cheat) {
    return (Level::CheatMaskField(cheat) & Level::CheatMask::kMultiplayer) != 0;
}

} // namespace Frigate::Game::Menu::Cheat
