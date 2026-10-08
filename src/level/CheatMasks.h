#pragma once

#include <cstdint>

namespace Frigate::Level {

// CHEAT_MASK: the bits of a g_CheatInfo maskfield.
namespace CheatMask {
constexpr uint32_t kMenus = 1u << 0;
constexpr uint32_t kSinglePlayer = 1u << 1;
constexpr uint32_t kMultiplayer = 1u << 2;
constexpr uint32_t kShowText = 1u << 4;
constexpr uint32_t kGlobal = 1u << 5;
constexpr uint32_t kToggle = 1u << 6;
} // namespace CheatMask

// g_CheatInfo[cheat - 1].maskfield; 0 for an id outside the table.
uint32_t CheatMaskField(int32_t cheat);

// The mask cheatTryActivateFromInputBuffer filters the cheats by in a stage
// of `playerCount` players.
uint32_t CheatApplicableMask(int playerCount);

} // namespace Frigate::Level
