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
    // One entry per head placeholder resolved by a HeadAttachment, in the
    // order their nodes are visited. A head file's lists all name matrix 0,
    // not an index shared with the body's array, so each attachment is a
    // self-contained walk rather than folded into the fields above.
    std::vector<ModelWalk> attachments;
};

// A head model to graft onto one of the body's kHeadPlaceholderNodeType
// nodes, keyed by that node's file offset (ModelNode::offset). Which head (if
// any) attaches where is a per-character choice the model file doesn't
// carry, so the caller supplies it.
struct HeadAttachment {
    uint32_t placeholderOffset;
    const std::vector<ModelNode>* nodes;
    uint32_t matrixCount;
};

// Walks the tree from its first node, composing each matrix-owning node's
// matrix as a translation by its origin followed by the nearest owning
// ancestor's (`base` above the root), and collects the lists of every node
// that has any. Matrices no node owns stay `base`. Every joint is at its
// rest pose: no animation rotation is applied.
//
// A kHeadPlaceholderNodeType node matching a HeadAttachment's offset has that
// head's tree walked in turn, based on the placeholder's own inherited
// matrix (a placeholder owns no matrix of its own, so this is the nearest
// owning ancestor's), and the result appended to ModelWalk::attachments.
ModelWalk WalkModel(const std::vector<ModelNode>& nodes, uint32_t matrixCount, const Mat4& base,
                    const std::vector<HeadAttachment>& headAttachments = {});

} // namespace Frigate
