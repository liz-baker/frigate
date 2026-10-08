#include "level/Cheats.h"

#include "level/CheatMasks.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

bool CheatShowsText(int32_t cheat) {
    return (CheatMaskField(cheat) & CheatMask::kShowText) != 0;
}

bool Cheats::IsActive(int32_t cheat, int player) const {
    if (cheat < 0 || static_cast<size_t>(cheat) >= marks_.size() || player < 0 || player >= 8) {
        return false;
    }
    return ((marks_[static_cast<size_t>(cheat)] >> player) & 1) != 0;
}

void Cheats::TurnOn(int32_t cheat, int currentPlayer, int playerCount) {
    if (cheat < 1 || cheat >= Game::Menu::Cheat::kInvalid || CheatShowsText(cheat)) {
        return;
    }
    uint8_t& mark = marks_[static_cast<size_t>(cheat)];
    if ((CheatMaskField(cheat) & CheatMask::kGlobal) == 0) {
        mark = static_cast<uint8_t>(mark | (1 << currentPlayer));
    } else {
        mark = static_cast<uint8_t>((1 << playerCount) - 1);
    }
}

void Cheats::TurnOff(int32_t cheat, int currentPlayer) {
    if (cheat < 1 || cheat >= Game::Menu::Cheat::kInvalid || CheatShowsText(cheat)) {
        return;
    }
    uint8_t& mark = marks_[static_cast<size_t>(cheat)];
    if ((CheatMaskField(cheat) & CheatMask::kGlobal) == 0) {
        mark = static_cast<uint8_t>(mark & ~(1 << currentPlayer));
    } else {
        mark = 0;
    }
}

bool EnemyRocketsReplaces(int32_t item) {
    // ITEM_KNIFE to ITEM_WATCHLASER, then the remote mine, its trigger and the taser.
    return (item >= kItemKnife && item <= kItemWatchLaser) || item == kItemRemoteMine || item == kItemTrigger ||
           item == kItemTaser;
}

} // namespace Frigate::Level
