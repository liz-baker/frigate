#include "level/CheatMasks.h"

#include <array>
#include <cstddef>

#include "game/menu/CheatIds.h"

namespace Frigate::Level {

namespace {

namespace Cheat = Game::Menu::Cheat;
using namespace CheatMask;

// g_CheatInfo's maskfield, from CHEAT_EXTRA_MP_CHARS. The save-unlock and
// stage-unlock entries after CHEAT_2X_LASER are all menu-only.
constexpr std::array<uint32_t, Cheat::kInvalid - 1> kMasks = [] {
    std::array<uint32_t, Cheat::kInvalid - 1> masks{};
    for (uint32_t& mask : masks) {
        mask = kShowText | kMenus;
    }
    const auto set = [&masks](int32_t cheat, uint32_t mask) { masks[static_cast<std::size_t>(cheat - 1)] = mask; };
    set(Cheat::kExtraMpChars, kGlobal | kShowText | kMenus);
    set(Cheat::kInvincibility, kMultiplayer | kSinglePlayer);
    set(Cheat::kAllGuns, kSinglePlayer);
    set(Cheat::kMaxAmmo, kShowText | kMultiplayer | kSinglePlayer);
    set(Cheat::kDebugUnk5, kShowText | kMultiplayer | kSinglePlayer);
    set(Cheat::kDeactivateInvincibility, kGlobal | kShowText | kMultiplayer);
    set(Cheat::kLineMode, kToggle | kGlobal | kMultiplayer | kSinglePlayer);
    set(Cheat::k2xHealth, kShowText | kMultiplayer | kSinglePlayer);
    set(Cheat::k2xArmor, kShowText | kMultiplayer | kSinglePlayer);
    set(Cheat::kInvisibility, kToggle | kGlobal | kSinglePlayer);
    set(Cheat::kInfiniteAmmo, kToggle | kGlobal | kMultiplayer | kSinglePlayer);
    set(Cheat::kDkMode, kGlobal | kMultiplayer | kSinglePlayer);
    set(Cheat::kExtraWeapons, kShowText | kSinglePlayer);
    set(Cheat::kTinyBond, kSinglePlayer);
    set(Cheat::kPaintball, kToggle | kGlobal | kMultiplayer | kSinglePlayer);
    set(Cheat::k10xHealth, kShowText | kMultiplayer | kSinglePlayer);
    set(Cheat::kMagnum, kShowText | kSinglePlayer);
    set(Cheat::kLaser, kShowText | kSinglePlayer);
    set(Cheat::kGoldenGun, kShowText | kSinglePlayer);
    set(Cheat::kSilverPp7, kShowText | kSinglePlayer);
    set(Cheat::kGoldPp7, kShowText | kSinglePlayer);
    set(Cheat::kBondPhase, kMultiplayer);
    set(Cheat::kNoRadarMp, kToggle | kGlobal | kMultiplayer);
    set(Cheat::kTurboMode, kToggle | kGlobal | kMultiplayer | kSinglePlayer);
    set(Cheat::kDebugPos, kGlobal);
    set(Cheat::kFastAnimation, kToggle | kGlobal | kShowText | kSinglePlayer);
    set(Cheat::kSlowAnimation, kToggle | kGlobal | kShowText | kSinglePlayer);
    set(Cheat::kEnemyRockets, kMenus);
    set(Cheat::k2xRocketLauncher, kShowText | kSinglePlayer);
    set(Cheat::k2xGrenadeLauncher, kShowText | kSinglePlayer);
    set(Cheat::k2xRcp90, kShowText | kSinglePlayer);
    set(Cheat::k2xThrowingKnife, kShowText | kSinglePlayer);
    set(Cheat::k2xHuntingKnife, kShowText | kSinglePlayer);
    set(Cheat::k2xLaser, kShowText | kSinglePlayer);
    return masks;
}();

} // namespace

uint32_t CheatMaskField(int32_t cheat) {
    if (cheat < 1 || cheat >= Cheat::kInvalid) {
        return 0;
    }
    return kMasks[static_cast<std::size_t>(cheat - 1)];
}

uint32_t CheatApplicableMask(int playerCount) {
    return playerCount == 1 ? kSinglePlayer : kMultiplayer;
}

} // namespace Frigate::Level
