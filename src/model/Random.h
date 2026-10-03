#pragma once

#include <cstdint>

namespace Frigate {

// The game's random number generator (randomGetNext()): a 64-bit shift-xor
// generator whose state is shared by everything that draws or simulates, and
// returns the low 32 bits of each new state.
uint32_t RandomGetNext();

// randomGetNextFrom(): the same generator stepping a caller's own state.
uint32_t RandomGetNextFrom(uint64_t* seed);

// randomSetSeed(): the state becomes `seed` plus one.
void RandomSetSeed(uint32_t seed);

// chrObjRandomGetNext(): the same generator over its own state
// (g_chrObjRandomSeed), which the object deformation draws from so the demo
// random byte does not depend on it.
uint32_t ChrObjRandomGetNext();
// chrObjRandomSetSeed(): the state becomes `seed` plus one.
void ChrObjRandomSetSeed(uint32_t seed);
uint64_t ChrObjRandomState();
void ChrObjRandomSetState(uint64_t state);

// g_randomSeed itself, which a demo's registers restore.
uint64_t RandomState();
void RandomSetState(uint64_t state);

} // namespace Frigate
