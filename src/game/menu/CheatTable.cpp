#include "game/menu/CheatTable.h"

#include <array>

#include "game/FrontState.h"

namespace Frigate::Game::Menu::Cheat {

namespace {

constexpr int32_t kNoText = -1;

struct Entry {
    int32_t textSlot;
    // CHEAT_MASK_MPGAME in the cheat's maskfield.
    bool multiplayer;
};

// g_CheatInfo, from CHEAT_EXTRA_MP_CHARS.
constexpr std::array<Entry, kCheatInvalid - 1> kEntries = { {
    { kNoText, false }, // CHEAT_EXTRA_MP_CHARS
    { 0x00, true },     // CHEAT_INVINCIBILITY
    { 0x01, false },    // CHEAT_ALLGUNS
    { kNoText, true },  // CHEAT_MAXAMMO
    { kNoText, true },  // CHEAT_DEBUG_UNK5
    { kNoText, true },  // CHEAT_DEACTIVATE_INVINCIBILITY
    { 0x37, true },     // CHEAT_LINEMODE
    { kNoText, true },  // CHEAT_2X_HEALTH
    { kNoText, true },  // CHEAT_2X_ARMOR
    { 0x04, false },    // CHEAT_INVISIBILITY
    { 0x06, true },     // CHEAT_INFINITE_AMMO
    { 0x07, true },     // CHEAT_DK_MODE
    { kNoText, false }, // CHEAT_EXTRA_WEAPONS
    { 0x08, false },    // CHEAT_TINY_BOND
    { 0x38, true },     // CHEAT_PAINTBALL
    { kNoText, true },  // CHEAT_10X_HEALTH
    { 0x0A, false },    // CHEAT_MAGNUM
    { 0x0B, false },    // CHEAT_LASER
    { 0x0C, false },    // CHEAT_GOLDEN_GUN
    { 0x0D, false },    // CHEAT_SILVER_PP7
    { 0x0E, false },    // CHEAT_GOLD_PP7
    { kNoText, true },  // CHEAT_BONDPHASE
    { 0x43, true },     // CHEAT_NO_RADAR_MP
    { 0x40, true },     // CHEAT_TURBO_MODE
    { kNoText, false }, // CHEAT_DEBUG_POS
    { 0x41, false },    // CHEAT_FAST_ANIMATION
    { 0x42, false },    // CHEAT_SLOW_ANIMATION
    { 0x39, false },    // CHEAT_ENEMY_ROCKETS
    { 0x3A, false },    // CHEAT_2X_ROCKET_LAUNCHER
    { 0x3B, false },    // CHEAT_2X_GRENADE_LAUNCHER
    { 0x3C, false },    // CHEAT_2X_RCP90
    { 0x3D, false },    // CHEAT_2X_THROWING_KNIFE
    { 0x3E, false },    // CHEAT_2X_HUNTING_KNIFE
    { 0x3F, false },    // CHEAT_2X_LASER
    { kNoText, false }, // CHEAT_UNLOCK_PAINTBALL
    { kNoText, false }, // CHEAT_UNLOCK_INVINCIBLE
    { kNoText, false }, // CHEAT_UNLOCK_DKMODE
    { kNoText, false }, // CHEAT_UNLOCK_2XGL
    { kNoText, false }, // CHEAT_UNLOCK_2XRL
    { kNoText, false }, // CHEAT_UNLOCK_TURBO
    { kNoText, false }, // CHEAT_UNLOCK_NORADAR
    { kNoText, false }, // CHEAT_UNLOCK_TINY
    { kNoText, false }, // CHEAT_UNLOCK_2XTKNIFE
    { kNoText, false }, // CHEAT_UNLOCK_FAST
    { kNoText, false }, // CHEAT_UNLOCK_INVIS
    { kNoText, false }, // CHEAT_UNLOCK_ENEMYRL
    { kNoText, false }, // CHEAT_UNLOCK_SLOW
    { kNoText, false }, // CHEAT_UNLOCK_SILVERPPK
    { kNoText, false }, // CHEAT_UNLOCK_2XHKNIFE
    { kNoText, false }, // CHEAT_UNLOCK_INFAMMO
    { kNoText, false }, // CHEAT_UNLOCK_2XFNP0
    { kNoText, false }, // CHEAT_UNLOCK_GOLDPPK
    { kNoText, false }, // CHEAT_UNLOCK_2XLASER
    { kNoText, false }, // CHEAT_UNLOCK_ALLGUNS
    { kNoText, false }, // CHEAT_UNLOCK_DAM
    { kNoText, false }, // CHEAT_UNLOCK_FACILITY
    { kNoText, false }, // CHEAT_UNLOCK_RUNWAY
    { kNoText, false }, // CHEAT_UNLOCK_SURFACE
    { kNoText, false }, // CHEAT_UNLOCK_BUNKER
    { kNoText, false }, // CHEAT_UNLOCK_SILO
    { kNoText, false }, // CHEAT_UNLOCK_FRIGATE
    { kNoText, false }, // CHEAT_UNLOCK_SURFACE2
    { kNoText, false }, // CHEAT_UNLOCK_BUNKER2
    { kNoText, false }, // CHEAT_UNLOCK_STATUE
    { kNoText, false }, // CHEAT_UNLOCK_ARCHIVES
    { kNoText, false }, // CHEAT_UNLOCK_STREETS
    { kNoText, false }, // CHEAT_UNLOCK_DEPOT
    { kNoText, false }, // CHEAT_UNLOCK_TRAIN
    { kNoText, false }, // CHEAT_UNLOCK_JUNGLE
    { kNoText, false }, // CHEAT_UNLOCK_CONTROL
    { kNoText, false }, // CHEAT_UNLOCK_CAVERNS
    { kNoText, false }, // CHEAT_UNLOCK_CRADLE
    { kNoText, false }, // CHEAT_UNLOCK_AZTEK
    { kNoText, false }, // CHEAT_UNLOCK_EGYPT
} };

} // namespace

std::optional<int32_t> MenuTextSlot(int32_t cheat) {
    if (cheat < 1 || cheat >= static_cast<int32_t>(kCheatInvalid)) {
        return std::nullopt;
    }
    const int32_t slot = kEntries[static_cast<size_t>(cheat - 1)].textSlot;
    return slot == kNoText ? std::nullopt : std::optional<int32_t>(slot);
}

bool IsMultiplayerCheat(int32_t cheat) {
    if (cheat < 1 || cheat >= static_cast<int32_t>(kCheatInvalid)) {
        return false;
    }
    return kEntries[static_cast<size_t>(cheat - 1)].multiplayer;
}

} // namespace Frigate::Game::Menu::Cheat
