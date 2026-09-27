#pragma once

#include <cstdint>

namespace Frigate {

// The game's random number generator (randomGetNext()): a 64-bit shift-xor
// generator whose state is shared by everything that draws or simulates, and
// returns the low 32 bits of each new state.
uint32_t RandomGetNext();

// randomSetSeed(): the state becomes `seed` plus one.
void RandomSetSeed(uint32_t seed);

// g_randomSeed itself, which a demo's registers restore.
uint64_t RandomState();
void RandomSetState(uint64_t state);

} // namespace Frigate
