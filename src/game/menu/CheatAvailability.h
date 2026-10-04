#pragma once

#include <cstdint>

namespace Frigate::Game::Save {
class SaveProgress;
}

namespace Frigate::Game::Menu {

// The level whose completion within its target time unlocks `cheat` (the save
// bit is that level's number), or -1 for a cheat that is not a level reward.
int32_t RewardLevel(int32_t cheat);

// frontCheckIfCheatIsUnlocked(): whether `cheat` (a Cheat::Id from
// Cheat::kExtraMpChars up to but not including Cheat::kInvalid) is open in
// `folder`. The decomp spins forever on any other id; this returns false.
bool IsCheatAvailable(const Save::SaveProgress& progress, int32_t folder, int32_t cheat);

} // namespace Frigate::Game::Menu
