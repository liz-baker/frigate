#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "model/Animation.h"
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

// A joint table entry's animation channel indices (ModelSkeleton's
// SkeletonJoint, without the resource-layer dependency): mtxA unmirrored,
// mtxB mirrored.
struct JointChannels {
    uint16_t mtxA = 0;
    uint16_t mtxB = 0;
};

// The animation state to apply to a group node's rotation: a clip, its
// model's joint table (indexed by ModelNode::jointId), the fractional frame
// to sample, and whether to use each joint's mirrored channel (matching the
// decomp's single per-model `flip`, e.g. for gun-hand animations reused on
// the other hand).
struct AnimationPose {
    const AnimationClip* clip = nullptr;
    const std::vector<JointChannels>* joints = nullptr;
    float frame = 0.0f;
    bool mirror = false;
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
//
// A kSwitchNodeType node's child is visited only if its offset is in
// visibleSwitches; a switch's on/off state is gameplay logic specific to the
// model (e.g. a character's held-item state), not something the model file
// or a generic walk can derive, so the caller supplies it. A switch not
// listed is off, matching the record's own zero-initialized rwdata before
// any such gameplay logic runs.
//
// A kLodNodeType node's child is visited only if the node's own inherited
// matrix's translation distance (view space, since base already includes
// the camera) falls in [lodMinDistance, lodMaxDistance); no caller input is
// needed since that translation already carries the distance.
//
// A kBspNodeType node's two child groups (its own child link, then its
// bspSecondChild) are both always visited, but a plane test against the
// node's own inherited matrix (bspPoint/bspVector/bspTransformMode)
// decides which group goes first -- a negative dot product visits the
// child-link group first, matching the decomp's front/back reorder.
//
// A group node (type 0x02/0x03) with a `pose` whose joint table has an entry
// for the node's jointId is rotated by that joint's sampled animation
// rotation before the origin translation; any other matrix-owning node
// (including a group node when `pose` is null, or its joint table has no
// entry) stays at rest pose exactly as before. `pose` is not passed down
// into a head attachment's own walk -- a head has no matrix-owning nodes of
// its own to animate.
ModelWalk WalkModel(const std::vector<ModelNode>& nodes, uint32_t matrixCount, const Mat4& base,
                    const std::vector<HeadAttachment>& headAttachments = {},
                    const std::vector<uint32_t>& visibleSwitches = {}, const AnimationPose* pose = nullptr);

} // namespace Frigate
