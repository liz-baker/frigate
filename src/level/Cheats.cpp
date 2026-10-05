#include "level/Cheats.h"

#include "level/WeaponTable.h"

namespace Frigate::Level {

bool CheatShowsText(int32_t cheat) {
    namespace Cheat = Game::Menu::Cheat;
    switch (cheat) {
        case Cheat::kInvincibility:
        case Cheat::kAllGuns:
        case Cheat::kLineMode:
        case Cheat::kInvisibility:
        case Cheat::kInfiniteAmmo:
        case Cheat::kDkMode:
        case Cheat::kTinyBond:
        case Cheat::kPaintball:
        case Cheat::kBondPhase:
        case Cheat::kNoRadarMp:
        case Cheat::kTurboMode:
        case Cheat::kDebugPos:
        case Cheat::kEnemyRockets:
            return false;
        default:
            return true;
    }
}

bool Cheats::IsActive(int32_t cheat) const {
    return cheat >= 0 && static_cast<size_t>(cheat) < active_.size() && active_[static_cast<size_t>(cheat)];
}

void Cheats::TurnOn(int32_t cheat) {
    if (cheat >= 0 && static_cast<size_t>(cheat) < active_.size() && !CheatShowsText(cheat)) {
        active_[static_cast<size_t>(cheat)] = true;
    }
}

void Cheats::TurnOff(int32_t cheat) {
    if (cheat >= 0 && static_cast<size_t>(cheat) < active_.size() && !CheatShowsText(cheat)) {
        active_[static_cast<size_t>(cheat)] = false;
    }
}

bool EnemyRocketsReplaces(int32_t item) {
    // ITEM_KNIFE to ITEM_WATCHLASER, then the remote mine, its trigger and the taser.
    return (item >= kItemKnife && item <= kItemWatchLaser) || item == kItemRemoteMine || item == kItemTrigger ||
           item == kItemTaser;
}

} // namespace Frigate::Level
