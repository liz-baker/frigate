#include "level/StainStore.h"

#include <cstdio>

using Frigate::Level::StainStore;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

} // namespace

int main() {
    StainStore store;
    store.Reset(1);
    Check(store.Allocate(0x1F4 - 100), "a block that fits is given");
    Check(store.Allocate(100), "so is the rest");
    Check(!store.Allocate(1), "and then the pool is spent");

    store.Reset(1);
    int count = 0;
    while (store.Allocate(10)) {
        count++;
    }
    Check(count == 20, "the 20 slots give 20 blocks, the last taking all that is left");

    store.Reset(2);
    Check(store.Allocate(0xBB8), "a multiplayer pool holds 3000 vertices");
    Check(!store.Allocate(1), "and no more");

    return gFailures == 0 ? 0 : 1;
}
