#pragma once

#include <array>
#include <cstdint>

#include "game/menu/CheatIds.h"

namespace Frigate::Level {

// g_CheatPlayerTextRelated: the cheats a stage keeps switched on, one bit per
// player, which cheatIsActive reads for the current player.
class Cheats {
  public:
    // initCheatTextBuffer.
    void Reset() {
        marks_.fill(0);
    }
    // cheatIsActive.
    bool IsActive(int32_t cheat, int player) const;
    // The mark cheatButtonHandleCheatsTurnedOn leaves for a cheat: only the
    // cheats without CHEAT_MASK_SHOW_TEXT are kept, for the current player or
    // (CHEAT_MASK_GLOBAL) for every player.
    void TurnOn(int32_t cheat, int currentPlayer, int playerCount);
    // The mark cheatButtonHandleCheatsTurnedOff clears.
    void TurnOff(int32_t cheat, int currentPlayer);

  private:
    std::array<uint8_t, Game::Menu::Cheat::kInvalid + 1> marks_{};
};

// Whether the cheat's maskfield in g_CheatInfo has CHEAT_MASK_SHOW_TEXT.
bool CheatShowsText(int32_t cheat);

// The weapons the enemy rockets cheat replaces with a rocket launcher.
bool EnemyRocketsReplaces(int32_t item);
// PROP_CHRROCKETLAUNCH: the held rocket launcher's entry in the prop model table.
constexpr int32_t kPropChrRocketLaunch = 211;

} // namespace Frigate::Level
