#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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

// A node the walk visited.
struct VisitedNode {
    uint32_t node;      // index into the walked node array
    int32_t attachment; // -1, or the ModelWalk::attachments index whose node array `node` indexes
    uint32_t depth;     // levels below the root; a head's nodes continue from its placeholder's depth
};

struct ModelWalk {
    // Indexed by the matrix numbers the model's display lists name.
    std::vector<Mat4> matrices;
    // Every node the walk reaches, in the order the game's tree traversal
    // visits them: gated-off children and everything below them are absent,
    // a head's nodes follow their placeholder, and a BSP node's two groups
    // come in the order its plane test picks.
    std::vector<VisitedNode> visited;
    // Every list to draw, in the order the tree yields them.
    std::vector<ListRef> lists;
    // One entry per head placeholder resolved by a HeadAttachment, in the
    // order their nodes are visited. A head file's lists all name matrix 0,
    // not an index shared with the body's array, so each attachment is a
    // self-contained walk rather than folded into the fields above.
    std::vector<ModelWalk> attachments;
};

// The matrix of the nearest node at or above `index` that owns one (the
// decomp's modelFindNodeMtx): a node's own, or its nearest matrix-owning
// ancestor's. nullptr if none does. `walk` is the walk of `nodes`.
const Mat4* FindNodeMatrix(const std::vector<ModelNode>& nodes, const ModelWalk& walk, int32_t index);

// A head model to graft onto one of the body's kHeadPlaceholderNodeType
// nodes, keyed by that node's file offset (ModelNode::offset). Which head (if
// any) attaches where is a per-character choice the model file doesn't
// carry, so the caller supplies it.
struct HeadAttachment {
    uint32_t placeholderOffset;
    const std::vector<ModelNode>* nodes;
    uint32_t matrixCount;
    // Offsets of the head's own switch nodes to treat as off (e.g. its
    // sunglasses).
    std::vector<uint32_t> hiddenSwitches = {};
};

// What the game's level-of-detail test depends on outside the tree: the
// decomp's g_ModelDistanceDisabled, the model's own scale, the player's
// lodscalez and g_ModelDistanceScale.
struct LodSettings {
    // Treat every node as at distance 0, so only LOD children with a minimum
    // distance of 0 show.
    bool distanceDisabled = false;
    float modelScale = 1.0f;
    float zScale = 1.0f;
    float distanceScale = 1.0f;
};

// A joint table entry's animation channel indices (ModelSkeleton's
// SkeletonJoint, without the resource-layer dependency): mtxA unmirrored,
// mtxB mirrored.
struct JointChannels {
    uint16_t mtxA = 0;
    uint16_t mtxB = 0;
};

// The animation state to apply to a group node's rotation: a clip, its
// model's joint table (indexed by ModelNode::jointId), the two frames to
// decode and the weight of the second (the decomp's framea/frameb and its
// per-model interpolation fraction; a fraction of 0 is `frameA` alone), and
// whether to use each joint's mirrored channel (the decomp's per-model
// `gunhand`, e.g. for gun-hand animations reused on the other hand).
// ModelPlayback (model/Playback.h) fills all of this in.
//
// `secondaryClip` optionally blends in a second clip's rotation (e.g. for
// transitions between poses) via quaternion slerp by `blend`, with its own
// frames and mirror flag. `blend` of 0 is exactly the primary rotation (the
// secondary clip has no effect); `secondaryClip` is null when the model has no
// secondary animation, and `blend` is meaningless without it.
struct AnimationPose {
    const AnimationClip* clip = nullptr;
    const std::vector<JointChannels>* joints = nullptr;
    bool mirror = false;
    uint32_t frameA = 0;
    uint32_t frameB = 0;
    float frameFrac = 0.0f;

    const AnimationClip* secondaryClip = nullptr;
    bool secondaryMirror = false;
    uint32_t secondaryFrameA = 0;
    uint32_t secondaryFrameB = 0;
    float secondaryFrameFrac = 0.0f;
    float blend = 0.0f;
};

// Called with each group node's first matrix, by index, as soon as the walk has
// built it and before anything below the node derives from it: the decomp's
// g_ModelJointPositionedFunc. It may change the matrix.
using JointHook = std::function<void(uint32_t matrixIndex, Mat4* matrix)>;

// Where a character's header (root) node stands: a rotation about Y by
// `heading` and a translation to `position`, with the rotation part scaled
// by `scale`, in place of the identity the header node otherwise owns.
struct RootPlacement {
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float heading = 0.0f;
    float scale = 1.0f;
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
// A kSwitchNodeType node's child is visited unless its offset is in
// hiddenSwitches; a switch's on/off state is gameplay logic specific to the
// model (e.g. a character's held-item state), not something the model file
// or a generic walk can derive, so the caller supplies the ones it turns
// off. Every switch starts on, as the decomp's modelInitRwData leaves it.
//
// A kLodNodeType node's child is visited only if the node's distance (its own
// inherited matrix's depth in view space, since base already includes the
// camera, times `lod`'s scales) is above lodMinDistance (or that minimum is 0)
// and no more than lodMaxDistance, both scaled by the model's scale; `lod`
// null is all scales 1 with the distance enabled.
//
// A kBspNodeType node's two child groups (its own child link, then its
// bspSecondChild) are both always visited, but a plane test against the
// node's own inherited matrix (bspPoint/bspVector/bspTransformMode)
// decides which group goes first -- a negative dot product visits the
// child-link group first, matching the decomp's front/back reorder.
//
// A header node (type 0x01) with a `root` is placed by it; without one it owns
// the inherited matrix unchanged.
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
                    const std::vector<uint32_t>& hiddenSwitches = {}, const AnimationPose* pose = nullptr,
                    const RootPlacement* root = nullptr, const LodSettings* lod = nullptr,
                    const JointHook* jointHook = nullptr);

// Walks the tree with the matrices already built, as the first-person gun code
// does: it fills the model's matrix array itself and never runs the per-node
// matrix builders. `matrices` is indexed by the matrix numbers the lists name;
// a node's matrix is the one its index picks, else its nearest owning
// ancestor's. Gating (switches, level of detail, BSP) is as WalkModel's.
ModelWalk WalkModelWithMatrices(const std::vector<ModelNode>& nodes, std::vector<Mat4> matrices,
                                const std::vector<uint32_t>& hiddenSwitches = {}, const LodSettings* lod = nullptr);

} // namespace Frigate
