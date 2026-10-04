#pragma once

#include <array>
#include <cstdint>

#include "game/menu/CheatIds.h"

namespace Frigate::Level {

// g_CheatPlayerTextRelated for the one player: the cheats a stage keeps
// switched on, which cheatIsActive reads.
class Cheats {
  public:
    // initCheatTextBuffer.
    void Reset() {
        active_.fill(false);
    }
    // cheatIsActive.
    bool IsActive(int32_t cheat) const;
    // The mark cheatButtonHandleCheatsTurnedOn leaves for a cheat: only the
    // cheats without CHEAT_MASK_SHOW_TEXT are kept.
    void TurnOn(int32_t cheat);

  private:
    std::array<bool, Game::Menu::Cheat::kInvalid + 1> active_{};
};

// The weapons the enemy rockets cheat replaces with a rocket launcher.
bool EnemyRocketsReplaces(int32_t item);
// PROP_CHRROCKETLAUNCH: the held rocket launcher's entry in the prop model table.
constexpr int32_t kPropChrRocketLaunch = 211;

} // namespace Frigate::Level
