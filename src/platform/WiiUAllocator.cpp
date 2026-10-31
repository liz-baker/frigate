// The Wii U's default heap is a first-fit free list, so once a stage has
// left it with many live and freed blocks, one allocation walks past
// thousands of them: a malloc/free pair went from 1us to over 100us during
// a stage, and in Dam the game's large allocations (about 64 a frame) went from
// 5us to about 80us each over four minutes, a slowdown of 5 ms a frame that
// only a menu would undo. Allocations are served from per-size free lists
// instead, which stay constant-time however fragmented the default heap gets:
//
//   small  up to 512 bytes, in 16-byte classes, never returned to the heap;
//   medium 513 bytes to 256 KiB, in power-of-two classes, kept for reuse up to a limit;
//   large  anything bigger, from the default heap.
#if defined(__WIIU__) || defined(FRIGATE_ALLOCATOR_HOST_TEST)
#include "platform/WiiUAllocator.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

constexpr size_t kHeader = 16; // keeps the returned pointer 16-byte aligned
constexpr size_t kGranularity = 16;
constexpr size_t kClassCount = 32;
constexpr size_t kMaxSmall = kGranularity * kClassCount;
constexpr size_t kChunkBytes = 1u << 20;

// What a block's header holds: a small class, a medium class, or large for a plain malloc.
constexpr uint32_t kMediumBase = 0x100;
constexpr uint32_t kLarge = 0xFFFFFFFFu;

constexpr uint32_t kMediumMinShift = 10;
constexpr uint32_t kMediumMaxShift = 18;
constexpr size_t kMediumClasses = kMediumMaxShift - kMediumMinShift + 1;
constexpr size_t kMaxMedium = size_t{ 1 } << kMediumMaxShift;
// A frame that allocates and frees the same buffers again and again needs only a few of each size; the
// limit stops a one-off burst (a load) from leaving that memory out of the heap for good.
constexpr size_t kMediumRetainLimit = size_t{ 32 } << 20;

struct FreeBlock {
    FreeBlock* next;
};

struct Pool {
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    FreeBlock* freeList[kClassCount] = {};
    FreeBlock* mediumFree[kMediumClasses] = {};
    size_t mediumRetained = 0;
    uint8_t* chunkCursor = nullptr;
    uint8_t* chunkEnd = nullptr;
};

Pool gPool;

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

size_t MediumBlockBytes(size_t cls) {
    return size_t{ 1 } << (kMediumMinShift + cls);
}

void* AllocSmall(size_t cls) {
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
    while (MediumBlockBytes(cls) < size) {
        cls++;
    }
    return cls;
}

void* AllocMedium(size_t size) {
    const size_t cls = MediumClass(size);
    {
        const Guard guard;
        if (FreeBlock* head = gPool.mediumFree[cls]) {
            gPool.mediumFree[cls] = head->next;
            gPool.mediumRetained -= MediumBlockBytes(cls);
            return head;
        }
    }
    auto* block = static_cast<uint8_t*>(std::malloc(MediumBlockBytes(cls) + kHeader));
    if (block == nullptr) {
        return nullptr;
    }
    *reinterpret_cast<uint32_t*>(block) = kMediumBase + static_cast<uint32_t>(cls);
    return block + kHeader;
}

void* AllocLarge(size_t size) {
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
    } else if (size <= kMaxMedium) {
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
        std::free(block);
        return;
    }
    auto* freed = static_cast<FreeBlock*>(pointer);
    if (cls >= kMediumBase) {
        const size_t mediumClass = cls - kMediumBase;
        {
            const Guard guard;
            if (gPool.mediumRetained + MediumBlockBytes(mediumClass) <= kMediumRetainLimit) {
                freed->next = gPool.mediumFree[mediumClass];
                gPool.mediumFree[mediumClass] = freed;
                gPool.mediumRetained += MediumBlockBytes(mediumClass);
                return;
            }
        }
        std::free(block);
        return;
    }
    const Guard guard;
    freed->next = gPool.freeList[cls];
    gPool.freeList[cls] = freed;
}

} // namespace

namespace Frigate::Platform {

size_t MediumBytesRetained() {
    const Guard guard;
    return gPool.mediumRetained;
}

size_t MediumRetainLimit() {
    return kMediumRetainLimit;
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
