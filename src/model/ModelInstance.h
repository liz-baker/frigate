#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/ModelWalk.h"
#include "model/Playback.h"
#include "resource/type/AnimationClip.h"
#include "resource/type/ModelSkeleton.h"
#include "resource/type/ModelTree.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

// One instance of a character, gun or prop model: its node tree and joint
// table, the playback state that animates it, and the head, hidden switches
// and holder that shape how it is posed. The decomp's Model plus what
// makeonebody and setup_chr_instance assemble around it.
namespace Frigate::Model {

class ModelInstance {
  public:
    // Loads `path`'s node tree (`<path>/tree`) and, if it has one, its joint
    // table (`<path>/skeleton`). Returns nullptr (after logging why) if the
    // tree doesn't load.
    static std::shared_ptr<ModelInstance> Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                                 const std::string& path);

    // As Create, from already-loaded parts. `skeleton` may be null.
    static std::shared_ptr<ModelInstance> FromParts(const std::string& path, std::shared_ptr<ModelTree> tree,
                                                    std::shared_ptr<ModelSkeleton> skeleton);

    const std::string& Path() const {
        return path_;
    }
    const ModelTree& Tree() const {
        return *tree_;
    }
    const ModelTree* HeadTree() const {
        return headTree_.get();
    }
    const std::string& HeadPath() const {
        return headPath_;
    }

    // Grafts the head model at `headPath` onto this body's head placeholder
    // (the node in switch-array slot 4), as makeonebody does. The head's first
    // switch (its sunglasses) is hidden unless `sunglasses`. Returns false
    // (after logging why) if there's no placeholder or the head doesn't load.
    bool AttachHead(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const std::string& headPath,
                    bool sunglasses);

    // As AttachHead, with the head's tree already loaded.
    bool AttachHeadTree(const std::string& headPath, std::shared_ptr<ModelTree> head, bool sunglasses);

    // Marks this model as held by `holder`'s node in switch-array slot
    // `switchIndex` (the decomp's attachedto and attachedto_objinst): its
    // matrices are then based on that node's, once `holder` is posed, turned
    // by `roll` radians about Z first. Returns false if that slot is empty.
    bool AttachTo(const std::shared_ptr<ModelInstance>& holder, size_t switchIndex, float roll = 0.0f);

    // The model is no longer held (attachedto_objinst becomes null): it is based
    // on the transform above its root again.
    void Detach();

    // The node in switch-array slot `index`, or nullptr for an empty slot.
    const ModelNode* SwitchNode(size_t index) const;
    // The length of the switch array (numSwitches) and the number of matrices
    // the model's lists address (numMatrices).
    size_t SwitchCount() const {
        return tree_->Switches.size();
    }
    size_t MatrixCount() const {
        return tree_->MatrixCount;
    }
    // modelFindNodeMtxIndex for the node in switch-array slot `index`: the index
    // of the matrix it, or its nearest owning ancestor, owns. -1 for none.
    int MatrixIndexOfSwitch(size_t index) const;
    // The same for a node by its index in the tree.
    int MatrixIndexOfNode(int32_t node) const;

    // Shows or hides the switch node in slot `index` (and what is below it).
    // Every switch starts shown.
    void SetSwitchVisible(size_t index, bool visible);

    // As SetSwitchVisible, for the switch in slot `index` of the attached head's
    // model (the sunglasses are slot 0). Does nothing without a head.
    void SetHeadSwitchVisible(size_t index, bool visible);

    // Shows or hides the muzzle flash of the gunfire node in switch-array slot
    // `index`, as the decomp's ModelRwData_GunfireRecord::visible does. Every
    // flash starts hidden.
    void SetGunfireVisible(size_t index, bool visible);
    bool GunfireVisible(uint32_t nodeOffset) const;

    // Loads `animations/<name>` and keeps it alive as long as this instance
    // is. Returns nullptr (after logging why) if it doesn't load.
    const AnimationClip* LoadClip(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                  const std::string& name);

    // Called for each of the model's joints as it is posed (see JointHook).
    JointHook jointHook;

    // Animation playback state. The joint table and root joint are set up by
    // Create; a caller sets `scale`, `animTranslationScale`, the animation,
    // and so on.
    ModelPlayback playback;

    // What the level-of-detail test depends on outside the tree. `modelScale`
    // follows playback.scale; the rest are the caller's.
    LodSettings lod;

    // setsuboffset() for a model whose root is a group node: replaces the
    // origin stored in the file. A model with another kind of root is left as
    // it is.
    void SetRootOrigin(const std::array<float, 3>& origin);

    // Poses the model for this frame: from `playback`, with `base` as the
    // transform above the root (the camera, for a character). A held model is
    // based on its holder's node instead; pose the holder first.
    void Pose(const Mat4& base);

    // As Pose, for a model whose matrices the caller built itself, indexed by
    // the matrix numbers the model's lists name (the first-person guns).
    void PoseWithMatrices(std::vector<Mat4> matrices);

    // modelInit's reset of the node state: every switch shown again and every
    // muzzle flash hidden.
    void ResetNodeState();

    // Applies `before` ahead of matrix 0 of the last Pose's result (the
    // decomp's matrix_4x4_multiply_homogeneous of matrix 0 by `before`), which
    // changes that matrix alone: the ones derived from it keep their values.
    void PrependToMatrix0(const Mat4& before);

    // Replaces the last Pose's matrices with `matrices`, for a caller that
    // places the model's parts itself after posing it.
    void ReplaceMatrices(std::vector<Mat4> matrices);

    // The last Pose's result.
    const ModelWalk& Walk() const {
        return walk_;
    }

    // The nodes the joint draw list reads from this instance.
    const std::vector<ModelNode>& Nodes() const {
        return ownNodes_ ? *ownNodes_ : tree_->Nodes;
    }
    const std::vector<ModelNode>* HeadNodes() const {
        return headTree_ != nullptr ? &headTree_->Nodes : nullptr;
    }

    // A vertex of a display-list node whose alpha the instance draws with in
    // place of the file's (the blood a hit paints on a body), and its colour
    // too when `colour` is set (the mission select's icons). `head` names the
    // grafted head's tree rather than the body's; `node` is the node's
    // ModelNode::offset.
    struct VertexPaint {
        bool head = false;
        uint32_t node = 0;
        int32_t vertex = 0;
        uint8_t alpha = 0;
        std::optional<std::array<uint8_t, 3>> colour;
    };
    void PaintVertex(const VertexPaint& paint) {
        paints_.push_back(paint);
    }
    void ClearPaints() {
        paints_.clear();
    }
    const std::vector<VertexPaint>& Paints() const {
        return paints_;
    }
    // The vertices (Vtx_t, 16 bytes each) the display-list node at `nodeOffset`
    // reads in place of its file's, for an object blown out of shape; null puts
    // the file's back. The vertices must outlive their use.
    void SetNodeVertices(uint32_t nodeOffset, const void* vertices, size_t count) {
        nodeVertices_ = vertices;
        nodeVerticesOffset_ = nodeOffset;
        nodeVerticesCount_ = count;
    }
    // The count is 0 and the vertices null when `nodeOffset` has none.
    const void* NodeVertices(uint32_t nodeOffset, size_t* count) const {
        if (nodeVertices_ != nullptr && nodeVerticesOffset_ == nodeOffset) {
            *count = nodeVerticesCount_;
            return nodeVertices_;
        }
        *count = 0;
        return nullptr;
    }

  private:
    ModelInstance() = default;

    const void* nodeVertices_ = nullptr;
    uint32_t nodeVerticesOffset_ = 0;
    size_t nodeVerticesCount_ = 0;

    std::string path_;
    std::shared_ptr<ModelTree> tree_;
    // A copy of the tree's nodes, once something about them differs from the
    // file's; the tree itself is shared with every other instance.
    std::optional<std::vector<ModelNode>> ownNodes_;
    std::vector<VertexPaint> paints_;
    std::shared_ptr<ModelSkeleton> skeleton_;
    std::vector<JointChannels> joints_;

    std::string headPath_;
    std::shared_ptr<ModelTree> headTree_;
    uint32_t headPlaceholder_ = 0;
    std::vector<uint32_t> headHiddenSwitches_;

    std::vector<uint32_t> hiddenSwitches_;
    std::vector<uint32_t> visibleGunfire_;
    std::vector<std::shared_ptr<AnimationClipResource>> clips_;

    std::shared_ptr<ModelInstance> holder_;
    int32_t holderNode_ = kNoNode;
    float holderRoll_ = 0.0f;

    ModelWalk walk_;
};

} // namespace Frigate::Model
