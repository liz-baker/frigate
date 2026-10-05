#pragma once

#include <cstdint>

namespace Frigate::Level {

// CUFF_TYPES: the Bond a cuff record of the intro picks (the cuffs the gun and
// watch models show, and the body the swirl shows).
namespace Cuff {
constexpr int32_t kBlue = 0;
constexpr int32_t kBrosnan = 1;
constexpr int32_t kJungle = 2;
constexpr int32_t kBoiler = 3;
constexpr int32_t kSnow = 4;
constexpr int32_t kConnery = 5;
constexpr int32_t kDalton = 6;
constexpr int32_t kMoore = 7;
// The Bond of the save folder.
constexpr int32_t kFolder = 8;
} // namespace Cuff

// BOND_*: the actors of a save folder.
namespace BondActor {
constexpr int32_t kBrosnan = 0;
constexpr int32_t kConnery = 1;
constexpr int32_t kDalton = 2;
constexpr int32_t kMoore = 3;
} // namespace BondActor

// fileGetBondForCurrentFolder and fileGetBondForFolder: the game as built has
// no way to pick another actor.
constexpr int32_t kFolderBond = BondActor::kBrosnan;

} // namespace Frigate::Level
