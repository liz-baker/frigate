// The Wii U allocator's pooling, built for the host: blocks keep their contents and alignment
// across sizes and threads, whichever way the medium pool is switched while they are live.
// Linking this replaces the global operator new and delete for this program, which is the point.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "platform/WiiUAllocator.h"

namespace {

std::atomic<int> failures{ 0 };

void Check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        failures++;
    }
}

struct Block {
    uint8_t* data;
    size_t size;
    uint8_t fill;
};

void Worker(uint32_t seed, int operations, bool toggle) {
    std::mt19937 rng(seed);
    const size_t sizes[] = {
        1, 15, 16, 17, 100, 511, 512, 513, 1024, 1025, 5000, 65536, 100000, 262144, 262145, 600000
    };
    std::vector<Block> live;
    for (int i = 0; i < operations; i++) {
        if (toggle && i % 997 == 0) {
            Frigate::Platform::SetMediumPool((i / 997) % 2 == 0);
        }
        if (live.size() < 64 && (live.empty() || rng() % 3 != 0)) {
            const size_t size = sizes[rng() % (sizeof(sizes) / sizeof(sizes[0]))];
            auto* data = new uint8_t[size];
            Check(reinterpret_cast<uintptr_t>(data) % 16 == 0, "16-byte alignment");
            const auto fill = static_cast<uint8_t>(rng());
            std::memset(data, fill, size);
            live.push_back({ data, size, fill });
        } else {
            const size_t at = rng() % live.size();
            Block b = live[at];
            live[at] = live.back();
            live.pop_back();
            bool intact = true;
            for (size_t k = 0; k < b.size; k += (b.size > 4096 ? 61 : 1)) {
                intact = intact && b.data[k] == b.fill;
            }
            intact = intact && b.data[b.size - 1] == b.fill;
            Check(intact, "contents survive other allocations");
            delete[] b.data;
        }
    }
    for (Block& b : live) {
        Check(b.data[0] == b.fill && b.data[b.size - 1] == b.fill, "contents at the end");
        delete[] b.data;
    }
}

} // namespace

int main() {
    // Single thread, pool off and on.
    Frigate::Platform::SetMediumPool(false);
    Worker(1, 20000, false);
    Frigate::Platform::SetMediumPool(true);
    Worker(2, 20000, false);

    // Four threads, switching the pool under them.
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < 4; t++) {
        threads.emplace_back(Worker, 100 + t, 30000, true);
    }
    for (auto& thread : threads) {
        thread.join();
    }

    // A block freed after the pool is switched off still goes back where it came from, and the
    // medium pool reuses it.
    // The addresses are compared as numbers saved before the delete, and the blocks are written to,
    // so the compiler can neither elide the allocations nor treat the stale pointer as unequal.
    Frigate::Platform::SetMediumPool(true);
    volatile uint8_t* first = new uint8_t[4000];
    first[0] = 1;
    const auto firstAddress = reinterpret_cast<uintptr_t>(first);
    delete[] const_cast<uint8_t*>(first);
    volatile uint8_t* second = new uint8_t[4000];
    second[0] = 2;
    Check(reinterpret_cast<uintptr_t>(second) == firstAddress, "medium pool reuses a freed block of its size");
    Frigate::Platform::SetMediumPool(false);
    delete[] const_cast<uint8_t*>(second);

    if (failures == 0) {
        std::puts("WiiUAllocatorTest: ok");
    }
    return failures == 0 ? 0 : 1;
}
