// A once-a-second read of how the Wii U's default heap is doing, for the profiler (Wii U builds
// with FAST3D_PROFILE only): what a malloc/free pair costs at a few sizes, and how much of the
// free space is one piece. A heap whose pairs get slower while the free space splits into smaller
// pieces is fragmenting. The hook also applies the medium_pool experiment.
#if defined(__WIIU__) && defined(FAST3D_PROFILE)
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/time.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "fast/profiler.h"
#include "platform/WiiUAllocator.h"

namespace Frigate::Platform {

namespace {

// The game registers this slot as the medium_pool experiment (Game.cpp).
constexpr int kMediumPoolExperiment = 4;

// Microseconds for one malloc/free pair, over `count` blocks allocated together and then freed.
double PairMicroseconds(size_t size, int count) {
    void* blocks[16];
    if (count > 16) {
        count = 16;
    }
    const OSTick start = OSGetSystemTick();
    for (int i = 0; i < count; i++) {
        blocks[i] = std::malloc(size);
    }
    for (int i = 0; i < count; i++) {
        std::free(blocks[i]);
    }
    return OSTicksToMicroseconds(static_cast<int64_t>(OSGetSystemTick() - start)) / static_cast<double>(count);
}

void Probe() {
    SetMediumPool(Fast::Prof::gExp.user[kMediumPoolExperiment] != 0);

    const double us1k = PairMicroseconds(1024, 16);
    const double us4k = PairMicroseconds(4096, 16);
    const double us32k = PairMicroseconds(32768, 16);
    const double us256k = PairMicroseconds(262144, 8);

    char line[240];
    const MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
    const uint32_t freeBytes = MEMGetTotalFreeSizeForExpHeap(heap);
    const uint32_t largest = MEMGetAllocatableSizeForExpHeapEx(heap, 4);
    std::snprintf(line, sizeof(line),
                  "[prof] heap probe: malloc+free us 1K=%.1f 4K=%.1f 32K=%.1f 256K=%.1f; MEM2 free %.1f MB, largest "
                  "piece %.1f MB",
                  us1k, us4k, us32k, us256k, freeBytes / 1048576.0, largest / 1048576.0);
    Fast::Prof::Note(line);
}

} // namespace

void InstallHeapProbe() {
    Fast::Prof::SetReportHook(Probe);
}

} // namespace Frigate::Platform
#endif
