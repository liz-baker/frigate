#pragma once

#include <cstdint>
#include <optional>

#include "game/menu/CheatIds.h"

namespace Frigate::Game::Menu::Cheat {

// g_CheatInfo's text_bank_id for a cheat, as its LMISC slot: the name the
// cheat menu lists it by. None for a cheat without one.
std::optional<int32_t> MenuTextSlot(int32_t cheat);

// cheatCheckIfMPCheat: whether the cheat applies in multiplayer.
bool IsMultiplayerCheat(int32_t cheat);

} // namespace Frigate::Game::Menu::Cheat
