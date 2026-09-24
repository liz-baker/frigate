#pragma once

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
    // matrices are then based on that node's, once `holder` is posed.
    // Returns false if that slot is empty.
    bool AttachTo(const std::shared_ptr<ModelInstance>& holder, size_t switchIndex);

    // The node in switch-array slot `index`, or nullptr for an empty slot.
    const ModelNode* SwitchNode(size_t index) const;

    // Shows or hides the switch node in slot `index` (and what is below it).
    // Every switch starts shown.
    void SetSwitchVisible(size_t index, bool visible);

    // Loads `animations/<name>` and keeps it alive as long as this instance
    // is. Returns nullptr (after logging why) if it doesn't load.
    const AnimationClip* LoadClip(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                  const std::string& name);

    // Animation playback state. The joint table and root joint are set up by
    // Create; a caller sets `scale`, `animTranslationScale`, the animation,
    // and so on.
    ModelPlayback playback;

    // What the level-of-detail test depends on outside the tree. `modelScale`
    // follows playback.scale; the rest are the caller's.
    LodSettings lod;

    // Poses the model for this frame: from `playback`, with `base` as the
    // transform above the root (the camera, for a character). A held model is
    // based on its holder's node instead; pose the holder first.
    void Pose(const Mat4& base);

    // The last Pose's result.
    const ModelWalk& Walk() const {
        return walk_;
    }

    // The nodes the joint draw list reads from this instance.
    const std::vector<ModelNode>& Nodes() const {
        return tree_->Nodes;
    }
    const std::vector<ModelNode>* HeadNodes() const {
        return headTree_ != nullptr ? &headTree_->Nodes : nullptr;
    }

  private:
    ModelInstance() = default;

    std::string path_;
    std::shared_ptr<ModelTree> tree_;
    std::shared_ptr<ModelSkeleton> skeleton_;
    std::vector<JointChannels> joints_;

    std::string headPath_;
    std::shared_ptr<ModelTree> headTree_;
    uint32_t headPlaceholder_ = 0;
    std::vector<uint32_t> headHiddenSwitches_;

    std::vector<uint32_t> hiddenSwitches_;
    std::vector<std::shared_ptr<AnimationClipResource>> clips_;

    std::shared_ptr<ModelInstance> holder_;
    int32_t holderNode_ = kNoNode;

    ModelWalk walk_;
};

} // namespace Frigate::Model
