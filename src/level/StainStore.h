#pragma once

#include <vector>

namespace Frigate::Level {

// The dynamic vertex store's character pool (vtxstore.c, tag 0xCCCC): the
// vertices a blood stain needs to paint a model's node with. Blocks are never
// given back, so a stage's stains run out.
class StainStore {
  public:
    // vtxstore_reset: the pool for a stage with `players` players.
    void Reset(int players);

    // vtxstore_allocate: whether a block of `count` vertices is left.
    bool Allocate(int count);

  private:
    // One block of the pool: its size, 0 users while free, and -1 for a slot
    // that holds no block.
    struct Block {
        int size = 0;
        int users = -1;
        int next = -1;
    };
    std::vector<Block> blocks_;
};

} // namespace Frigate::Level
