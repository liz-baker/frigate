#pragma once

#include <cstdint>

namespace Frigate::Game::Save {
class SaveProgress;
}

namespace Frigate::Game::Menu {

// frontCheckIfCheatIsUnlocked(): whether `cheat` (a Cheat::Id from
// Cheat::kExtraMpChars up to but not including Cheat::kInvalid) is open in
// `folder`. The decomp spins forever on any other id; this returns false.
bool IsCheatAvailable(const Save::SaveProgress& progress, int32_t folder, int32_t cheat);

} // namespace Frigate::Game::Menu
