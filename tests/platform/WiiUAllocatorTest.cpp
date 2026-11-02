// The Wii U allocator's pooling, built for the host: blocks keep their contents and alignment
// across sizes and threads, freed medium blocks are reused, and what is held back stays under
// the limit. Linking this replaces the global operator new and delete for this program, which
// is the point.
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

void Worker(uint32_t seed, int operations) {
    std::mt19937 rng(seed);
    const size_t sizes[] = {
        1, 15, 16, 17, 100, 511, 512, 513, 1024, 1025, 5000, 65536, 100000, 262144, 262145, 600000
    };
    std::vector<Block> live;
    for (int i = 0; i < operations; i++) {
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
    Worker(1, 20000);

    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < 4; t++) {
        threads.emplace_back(Worker, 100 + t, 30000);
    }
    for (auto& thread : threads) {
        thread.join();
    }

    // The addresses are compared as numbers saved before the delete, and the blocks are written to,
    // so the compiler can neither elide the allocations nor treat the stale pointer as unequal.
    volatile uint8_t* first = new uint8_t[4000];
    first[0] = 1;
    const auto firstAddress = reinterpret_cast<uintptr_t>(first);
    delete[] const_cast<uint8_t*>(first);
    volatile uint8_t* second = new uint8_t[4000];
    second[0] = 2;
    Check(reinterpret_cast<uintptr_t>(second) == firstAddress, "a freed medium block is reused");
    delete[] const_cast<uint8_t*>(second);

    // A burst larger than the limit is not all held on to.
    std::vector<uint8_t*> burst;
    const size_t blockSize = 200000;
    const size_t count = (Frigate::Platform::MediumRetainLimit() / 262144) * 3;
    for (size_t i = 0; i < count; i++) {
        burst.push_back(new uint8_t[blockSize]);
    }
    for (uint8_t* block : burst) {
        delete[] block;
    }
    Check(Frigate::Platform::MediumBytesRetained() <= Frigate::Platform::MediumRetainLimit(),
          "retained medium memory stays under the limit");

    if (failures == 0) {
        std::puts("WiiUAllocatorTest: ok");
    }
    return failures == 0 ? 0 : 1;
}
