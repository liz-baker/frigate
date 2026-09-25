#include "model/Random.h"

namespace Frigate {

namespace {

// g_randomSeed's initial value.
uint64_t gSeed = 0xAB8D9F7781280783ull;

} // namespace

uint32_t RandomGetNext() {
    const uint64_t state = gSeed;
    const uint64_t mixed = (((state & 1) << 32) | ((state & 0x1FFFFFFFFull) >> 1)) ^ ((state & 0xFFFFF) << 12);
    gSeed = (((mixed >> 20) & 0xFFF) ^ mixed);
    return static_cast<uint32_t>(gSeed);
}

void RandomSetSeed(uint32_t seed) {
    gSeed = static_cast<uint64_t>(seed) + 1;
}

uint64_t RandomState() {
    return gSeed;
}

void RandomSetState(uint64_t state) {
    gSeed = state;
}

} // namespace Frigate
