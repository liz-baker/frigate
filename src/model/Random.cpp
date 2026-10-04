#include "model/Random.h"

#include <chrono>

namespace Frigate {

namespace {

// g_randomSeed's initial value.
uint64_t gSeed = 0xAB8D9F7781280783ull;
// g_chrObjRandomSeed's initial value.
uint64_t gChrObjSeed = 0xAB8D9F7781280783ull;

uint32_t Advance(uint64_t* seed) {
    return RandomGetNextFrom(seed);
}

} // namespace

uint32_t RandomGetNextFrom(uint64_t* seed) {
    // The step every generator takes.
    const uint64_t state = *seed;
    const uint64_t mixed = (((state & 1) << 32) | ((state & 0x1FFFFFFFFull) >> 1)) ^ ((state & 0xFFFFF) << 12);
    *seed = (((mixed >> 20) & 0xFFF) ^ mixed);
    return static_cast<uint32_t>(*seed);
}

uint32_t RandomGetNext() {
    return Advance(&gSeed);
}

uint32_t ChrObjRandomGetNext() {
    return Advance(&gChrObjSeed);
}

void ChrObjRandomSetSeed(uint32_t seed) {
    gChrObjSeed = static_cast<uint64_t>(seed) + 1;
}

uint64_t ChrObjRandomState() {
    return gChrObjSeed;
}

void ChrObjRandomSetState(uint64_t state) {
    gChrObjSeed = state;
}

void RandomSetSeed(uint32_t seed) {
    gSeed = static_cast<uint64_t>(seed) + 1;
}

void RandomSeedFromClock() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    RandomSetSeed(static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() * 3 / 64));
}

uint64_t RandomState() {
    return gSeed;
}

void RandomSetState(uint64_t state) {
    gSeed = state;
}

} // namespace Frigate
