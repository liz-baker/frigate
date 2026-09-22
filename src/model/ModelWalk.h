#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "model/ModelNode.h"

namespace Frigate {

// Row-vector convention, as on the N64: translation lives in row 3.
struct Mat4 {
    float m[4][4];
};

Mat4 IdentityMat4();

struct ListRef {
    uint32_t node; // ModelNode::offset
    uint32_t list;
};

struct ModelWalk {
    // Indexed by the matrix numbers the model's display lists name.
    std::vector<Mat4> matrices;
    // Every list to draw, in the order the tree yields them.
    std::vector<ListRef> lists;
};

// Walks the tree from its first node, composing each matrix-owning node's
// matrix as a translation by its origin followed by the nearest owning
// ancestor's (`base` above the root), and collects the lists of every node
// that has any. Matrices no node owns stay `base`. Every joint is at its
// rest pose: no animation rotation is applied.
ModelWalk WalkModel(const std::vector<ModelNode>& nodes, uint32_t matrixCount, const Mat4& base);

} // namespace Frigate
