#include "model/ModelInstance.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Model {

namespace {

constexpr uint8_t kHeaderNodeType = 0x01;
// The switch-array slot holding a body's head placeholder.
constexpr size_t kHeadSwitchSlot = 4;

template <typename T>
std::shared_ptr<T> Load(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const std::string& path) {
    return std::dynamic_pointer_cast<T>(resourceManager->LoadResource(CRC64(path.c_str())));
}

} // namespace

std::shared_ptr<ModelInstance> ModelInstance::Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                                     const std::string& path) {
    auto tree = Load<ModelTree>(resourceManager, path + "/tree");
    if (tree == nullptr || tree->Nodes.empty()) {
        spdlog::error("model: no node tree at '{}/tree' in the loaded archives", path);
        return nullptr;
    }

    // Heads and props have no joint table.
    return FromParts(path, tree, Load<ModelSkeleton>(resourceManager, path + "/skeleton"));
}

std::shared_ptr<ModelInstance> ModelInstance::FromParts(const std::string& path, std::shared_ptr<ModelTree> tree,
                                                        std::shared_ptr<ModelSkeleton> skeleton) {
    std::shared_ptr<ModelInstance> instance(new ModelInstance());
    instance->path_ = path;
    instance->tree_ = std::move(tree);
    instance->skeleton_ = std::move(skeleton);
    if (instance->skeleton_ != nullptr) {
        for (const SkeletonJoint& joint : instance->skeleton_->Joints) {
            instance->joints_.push_back({ joint.mtxA, joint.mtxB });
        }
    }

    ModelPlayback& playback = instance->playback;
    playback.joints = &instance->joints_;
    const ModelNode& root = instance->tree_->Nodes[0];
    if (root.type == kHeaderNodeType) {
        playback.hasRoot = true;
        playback.rootJoint = root.jointId;
    }
    return instance;
}

bool ModelInstance::AttachHead(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                               const std::string& headPath, bool sunglasses) {
    auto head = Load<ModelTree>(resourceManager, headPath + "/tree");
    if (head == nullptr) {
        spdlog::error("model: no node tree at '{}/tree' in the loaded archives", headPath);
        return false;
    }
    return AttachHeadTree(headPath, std::move(head), sunglasses);
}

bool ModelInstance::AttachHeadTree(const std::string& headPath, std::shared_ptr<ModelTree> head, bool sunglasses) {
    const ModelNode* placeholder = SwitchNode(kHeadSwitchSlot);
    if (placeholder == nullptr || placeholder->type != kHeadPlaceholderNodeType) {
        spdlog::error("model: '{}' has no head placeholder to attach '{}' to", path_, headPath);
        return false;
    }

    headPath_ = headPath;
    headTree_ = std::move(head);
    headPlaceholder_ = placeholder->offset;
    headHiddenSwitches_.clear();
    if (!sunglasses && !headTree_->Switches.empty() && headTree_->Switches[0] != kNoNode) {
        headHiddenSwitches_.push_back(headTree_->Nodes[headTree_->Switches[0]].offset);
    }
    return true;
}

bool ModelInstance::AttachTo(const std::shared_ptr<ModelInstance>& holder, size_t switchIndex) {
    if (holder == nullptr || holder->SwitchNode(switchIndex) == nullptr) {
        spdlog::error("model: '{}' can't attach to an empty switch slot {}", path_, switchIndex);
        return false;
    }
    holder_ = holder;
    holderNode_ = holder->tree_->Switches[switchIndex];
    return true;
}

const ModelNode* ModelInstance::SwitchNode(size_t index) const {
    if (index >= tree_->Switches.size() || tree_->Switches[index] == kNoNode) {
        return nullptr;
    }
    return &tree_->Nodes[tree_->Switches[index]];
}

void ModelInstance::SetSwitchVisible(size_t index, bool visible) {
    const ModelNode* node = SwitchNode(index);
    if (node == nullptr) {
        return;
    }
    auto hidden = std::find(hiddenSwitches_.begin(), hiddenSwitches_.end(), node->offset);
    if (visible && hidden != hiddenSwitches_.end()) {
        hiddenSwitches_.erase(hidden);
    } else if (!visible && hidden == hiddenSwitches_.end()) {
        hiddenSwitches_.push_back(node->offset);
    }
}

const AnimationClip* ModelInstance::LoadClip(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                             const std::string& name) {
    auto clip = Load<AnimationClipResource>(resourceManager, "animations/" + name);
    if (clip == nullptr) {
        spdlog::error("model: no animation clip 'animations/{}' in the loaded archives", name);
        return nullptr;
    }
    if (std::find(clips_.begin(), clips_.end(), clip) == clips_.end()) {
        clips_.push_back(clip);
    }
    return &clip->View;
}

void ModelInstance::Pose(const Mat4& base) {
    Mat4 origin = base;
    if (holder_ != nullptr) {
        if (const Mat4* held = FindNodeMatrix(holder_->Nodes(), holder_->walk_, holderNode_)) {
            origin = *held;
        }
    }

    lod.modelScale = playback.scale;

    AnimationPose pose = MakePose(playback);
    RootPlacement placement = MakePlacement(playback);

    std::vector<HeadAttachment> heads;
    if (headTree_ != nullptr) {
        heads.push_back({ headPlaceholder_, &headTree_->Nodes, headTree_->MatrixCount, headHiddenSwitches_ });
    }

    walk_ = WalkModel(tree_->Nodes, tree_->MatrixCount, origin, heads, hiddenSwitches_,
                      playback.anim != nullptr ? &pose : nullptr, playback.hasRoot ? &placement : nullptr, &lod);
}

} // namespace Frigate::Model
