#include "level/StainStore.h"

namespace Frigate::Level {

namespace {

// dword_CODE_bss_8007A0D0 and ...D4: the pool's vertices and blocks.
constexpr int kVertices = 0x1F4;
constexpr int kBlocks = 0x14;
constexpr int kMultiplayerVertices = 0xBB8;
constexpr int kMultiplayerBlocks = 0x50;

} // namespace

void StainStore::Reset(int players) {
    const int blocks = players >= 2 ? kMultiplayerBlocks : kBlocks;
    blocks_.assign(static_cast<size_t>(blocks), Block{});
    blocks_[0].size = players >= 2 ? kMultiplayerVertices : kVertices;
    blocks_[0].users = 0;
}

bool StainStore::Allocate(int count) {
    const int blockCount = static_cast<int>(blocks_.size());
    int at = 0;
    int visited = 0;
    int state = 0;
    while (state == 0) {
        const Block& block = blocks_[static_cast<size_t>(at)];
        if (block.users == 0 && block.size >= count) {
            state = 1;
        } else {
            at = block.next;
            visited++;
            if (at == -1 || blockCount < visited) {
                state = -1;
            }
        }
    }
    if (state != 1) {
        return false;
    }

    Block& block = blocks_[static_cast<size_t>(at)];
    const int size = block.size;
    block.users++;
    if (size == count) {
        return true;
    }
    for (size_t slot = 0; slot < blocks_.size(); slot++) {
        Block& spare = blocks_[slot];
        if (spare.users == -1) {
            block.size = count;
            spare.size = size - count;
            spare.users = 0;
            spare.next = block.next;
            block.next = static_cast<int>(slot);
            break;
        }
    }
    return true;
}

} // namespace Frigate::Level
