// The Wii U's default heap is a first-fit free list, so once a stage has
// left it with many live and freed small blocks, one allocation walks past
// thousands of them: a malloc/free pair went from 1us to over 100us during
// a stage. Small C++ allocations are served from per-size free lists
// instead, which stay constant-time however fragmented the default heap gets.
#ifdef __WIIU__
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

constexpr size_t kHeader = 16; // keeps the returned pointer 16-byte aligned
constexpr size_t kGranularity = 16;
constexpr size_t kClassCount = 32;
constexpr size_t kMaxSmall = kGranularity * kClassCount;
constexpr size_t kChunkBytes = 1u << 20;
constexpr uint32_t kLarge = 0xFFFFFFFFu;

struct FreeBlock {
    FreeBlock* next;
};

struct Pool {
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    FreeBlock* freeList[kClassCount] = {};
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

void* Allocate(size_t size) {
    if (size == 0) {
        size = 1;
    }
    void* result;
    if (size <= kMaxSmall) {
        result = AllocSmall((size - 1) / kGranularity);
    } else {
        auto* block = static_cast<uint8_t*>(std::malloc(size + kHeader));
        if (block != nullptr) {
            *reinterpret_cast<uint32_t*>(block) = kLarge;
            block += kHeader;
        }
        result = block;
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
    const Guard guard;
    freed->next = gPool.freeList[cls];
    gPool.freeList[cls] = freed;
}

} // namespace

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
