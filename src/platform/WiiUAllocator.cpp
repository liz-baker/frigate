// The Wii U's default heap is a first-fit free list, so once a stage has
// left it with many live and freed small blocks, one allocation walks past
// thousands of them: a malloc/free pair went from 1us to over 100us during
// a stage. Small C++ allocations are served from per-size free lists
// instead, which stay constant-time however fragmented the default heap gets.
//
// Profiler builds (FAST3D_PROFILE) time the allocator's paths as stages of their own and count
// the allocations; medium blocks can be pooled too (SetMediumPool, the medium_pool experiment in
// docs/wiiu-profiling.md).
#if defined(__WIIU__) || defined(FRIGATE_ALLOCATOR_HOST_TEST)
#include "platform/WiiUAllocator.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#ifdef FAST3D_PROFILE
#include "fast/profiler.h"
#endif

namespace {

constexpr size_t kHeader = 16; // keeps the returned pointer 16-byte aligned
constexpr size_t kGranularity = 16;
constexpr size_t kClassCount = 32;
constexpr size_t kMaxSmall = kGranularity * kClassCount;
constexpr size_t kChunkBytes = 1u << 20;

// What a block's header holds: a small class, a medium class, or large for a plain malloc.
constexpr uint32_t kMediumBase = 0x100;
constexpr uint32_t kLarge = 0xFFFFFFFFu;

// Medium blocks are powers of two from 1 KiB to 256 KiB. Once a size has been allocated its blocks
// are kept on a free list for the next allocation of that size, so a frame that allocates and frees
// the same large buffers every time never goes back to the default heap.
constexpr uint32_t kMediumMinShift = 10;
constexpr uint32_t kMediumMaxShift = 18;
constexpr size_t kMediumClasses = kMediumMaxShift - kMediumMinShift + 1;
constexpr size_t kMaxMedium = size_t{ 1 } << kMediumMaxShift;

struct FreeBlock {
    FreeBlock* next;
};

struct Pool {
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    FreeBlock* freeList[kClassCount] = {};
    FreeBlock* mediumFree[kMediumClasses] = {};
    uint8_t* chunkCursor = nullptr;
    uint8_t* chunkEnd = nullptr;
};

Pool gPool;
std::atomic<bool> gMediumPool{ false };

struct Guard {
    Guard() {
        while (gPool.lock.test_and_set(std::memory_order_acquire)) {
            continue;
        }
    }
    ~Guard() {
        gPool.lock.clear(std::memory_order_release);
    }
};

void* AllocSmall(size_t cls) {
#ifdef FAST3D_PROFILE
    PROF_SCOPE(USER12);
    PROF_COUNT(USER_B);
#endif
    const size_t blockBytes = kHeader + ((cls + 1) * kGranularity);
    uint8_t* block;
    {
        const Guard guard;
        if (FreeBlock* head = gPool.freeList[cls]) {
            gPool.freeList[cls] = head->next;
            return head;
        }
        if (gPool.chunkCursor == nullptr || static_cast<size_t>(gPool.chunkEnd - gPool.chunkCursor) < blockBytes) {
            auto* chunk = static_cast<uint8_t*>(std::malloc(kChunkBytes));
            if (chunk == nullptr) {
                return nullptr;
            }
            gPool.chunkCursor = chunk;
            gPool.chunkEnd = chunk + kChunkBytes;
        }
        block = gPool.chunkCursor;
        gPool.chunkCursor += blockBytes;
    }
    *reinterpret_cast<uint32_t*>(block) = static_cast<uint32_t>(cls);
    return block + kHeader;
}

// The smallest medium class that holds `size`.
size_t MediumClass(size_t size) {
    size_t cls = 0;
    while ((size_t{ 1 } << (kMediumMinShift + cls)) < size) {
        cls++;
    }
    return cls;
}

void* AllocMedium(size_t size) {
#ifdef FAST3D_PROFILE
    PROF_SCOPE(USER14);
    PROF_COUNT(USER_C);
#endif
    const size_t cls = MediumClass(size);
    {
        const Guard guard;
        if (FreeBlock* head = gPool.mediumFree[cls]) {
            gPool.mediumFree[cls] = head->next;
            return head;
        }
    }
    auto* block = static_cast<uint8_t*>(std::malloc((size_t{ 1 } << (kMediumMinShift + cls)) + kHeader));
    if (block == nullptr) {
        return nullptr;
    }
    *reinterpret_cast<uint32_t*>(block) = kMediumBase + static_cast<uint32_t>(cls);
    return block + kHeader;
}

void* AllocLarge(size_t size) {
#ifdef FAST3D_PROFILE
    PROF_SCOPE(USER13);
    PROF_COUNT(USER_D);
#endif
    auto* block = static_cast<uint8_t*>(std::malloc(size + kHeader));
    if (block != nullptr) {
        *reinterpret_cast<uint32_t*>(block) = kLarge;
        block += kHeader;
    }
    return block;
}

void* Allocate(size_t size) {
    if (size == 0) {
        size = 1;
    }
    void* result;
    if (size <= kMaxSmall) {
        result = AllocSmall((size - 1) / kGranularity);
    } else if (size <= kMaxMedium && gMediumPool.load(std::memory_order_relaxed)) {
        result = AllocMedium(size);
    } else {
        result = AllocLarge(size);
    }
    if (result == nullptr) {
        throw std::bad_alloc();
    }
    return result;
}

void Release(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    auto* block = static_cast<uint8_t*>(pointer) - kHeader;
    const uint32_t cls = *reinterpret_cast<uint32_t*>(block);
    if (cls == kLarge) {
#ifdef FAST3D_PROFILE
        PROF_SCOPE(USER13);
#endif
        std::free(block);
        return;
    }
    auto* freed = static_cast<FreeBlock*>(pointer);
    const Guard guard;
    if (cls >= kMediumBase) {
        freed->next = gPool.mediumFree[cls - kMediumBase];
        gPool.mediumFree[cls - kMediumBase] = freed;
        return;
    }
    freed->next = gPool.freeList[cls];
    gPool.freeList[cls] = freed;
}

} // namespace

namespace Frigate::Platform {

void SetMediumPool(bool enabled) {
    gMediumPool.store(enabled, std::memory_order_relaxed);
}

} // namespace Frigate::Platform

void* operator new(size_t size) {
    return Allocate(size);
}

void* operator new[](size_t size) {
    return Allocate(size);
}

void operator delete(void* pointer) noexcept {
    Release(pointer);
}

void operator delete[](void* pointer) noexcept {
    Release(pointer);
}

void operator delete(void* pointer, size_t) noexcept {
    Release(pointer);
}

void operator delete[](void* pointer, size_t) noexcept {
    Release(pointer);
}
#endif
