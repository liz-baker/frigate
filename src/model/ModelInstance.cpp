#include "model/ModelInstance.h"

#include <algorithm>
#include <cmath>

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

// `held` with a rotation of `roll` about Z applied before it
// (matrix_4x4_set_rotation_around_z, then matrix_4x4_multiply_in_place).
Mat4 Rolled(const Mat4& held, float roll) {
    const float c = std::cos(roll);
    const float s = std::sin(roll);
    Mat4 out = held;
    for (int col = 0; col < 4; col++) {
        out.m[0][col] = c * held.m[0][col] + s * held.m[1][col];
        out.m[1][col] = -s * held.m[0][col] + c * held.m[1][col];
    }
    return out;
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

void ModelInstance::Detach() {
    holder_.reset();
    holderNode_ = kNoNode;
    holderRoll_ = 0.0f;
}

bool ModelInstance::AttachTo(const std::shared_ptr<ModelInstance>& holder, size_t switchIndex, float roll) {
    if (holder == nullptr || holder->SwitchNode(switchIndex) == nullptr) {
        spdlog::error("model: '{}' can't attach to an empty switch slot {}", path_, switchIndex);
        return false;
    }
    holder_ = holder;
    holderNode_ = holder->tree_->Switches[switchIndex];
    holderRoll_ = roll;
    return true;
}

const ModelNode* ModelInstance::SwitchNode(size_t index) const {
    if (index >= tree_->Switches.size() || tree_->Switches[index] == kNoNode) {
        return nullptr;
    }
    return &tree_->Nodes[tree_->Switches[index]];
}

int ModelInstance::MatrixIndexOfSwitch(size_t index) const {
    if (index >= tree_->Switches.size()) {
        return -1;
    }
    return MatrixIndexOfNode(tree_->Switches[index]);
}

int ModelInstance::MatrixIndexOfNode(int32_t node) const {
    const std::vector<ModelNode>& nodes = Nodes();
    int32_t at = node;
    // A malformed parent chain is bounded by the node count.
    for (size_t hops = 0; at != kNoNode && hops <= nodes.size(); hops++) {
        const ModelNode& current = nodes[at];
        const bool owns = current.type == 0x01 || current.type == 0x02 || current.type == 0x03 || current.type == 0x15;
        if (owns && current.matrix != kNoMatrix) {
            return current.matrix;
        }
        at = current.parent;
    }
    return -1;
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

void ModelInstance::SetHeadSwitchVisible(size_t index, bool visible) {
    if (headTree_ == nullptr || index >= headTree_->Switches.size() || headTree_->Switches[index] == kNoNode) {
        return;
    }
    const uint32_t offset = headTree_->Nodes[headTree_->Switches[index]].offset;
    auto hidden = std::find(headHiddenSwitches_.begin(), headHiddenSwitches_.end(), offset);
    if (visible && hidden != headHiddenSwitches_.end()) {
        headHiddenSwitches_.erase(hidden);
    } else if (!visible && hidden == headHiddenSwitches_.end()) {
        headHiddenSwitches_.push_back(offset);
    }
}

void ModelInstance::SetGunfireVisible(size_t index, bool visible) {
    const ModelNode* node = SwitchNode(index);
    if (node == nullptr || node->type != kGunfireNodeType) {
        return;
    }
    auto shown = std::find(visibleGunfire_.begin(), visibleGunfire_.end(), node->offset);
    if (visible && shown == visibleGunfire_.end()) {
        visibleGunfire_.push_back(node->offset);
    } else if (!visible && shown != visibleGunfire_.end()) {
        visibleGunfire_.erase(shown);
    }
}

bool ModelInstance::GunfireVisible(uint32_t nodeOffset) const {
    return std::find(visibleGunfire_.begin(), visibleGunfire_.end(), nodeOffset) != visibleGunfire_.end();
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

void ModelInstance::SetRootOrigin(const std::array<float, 3>& origin) {
    const uint8_t rootType = tree_->Nodes[0].type;
    if (rootType != 0x02 && rootType != 0x03) {
        return;
    }
    if (!ownNodes_) {
        ownNodes_ = tree_->Nodes;
    }
    std::copy(origin.begin(), origin.end(), (*ownNodes_)[0].origin);
}

void ModelInstance::PrependToMatrix0(const Mat4& before) {
    if (walk_.matrices.empty()) {
        return;
    }
    // matrix_4x4_multiply_homogeneous(matrix 0, before): `before`, then matrix 0.
    const Mat4 current = walk_.matrices[0];
    Mat4 out{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            out.m[j][i] =
                current.m[0][i] * before.m[j][0] + current.m[1][i] * before.m[j][1] + current.m[2][i] * before.m[j][2];
            if (j == 3) {
                out.m[j][i] += current.m[3][i];
            }
        }
    }
    out.m[3][3] = 1.0f;
    walk_.matrices[0] = out;
}

void ModelInstance::ReplaceMatrices(std::vector<Mat4> matrices) {
    walk_.matrices = std::move(matrices);
}

void ModelInstance::PoseWithMatrices(std::vector<Mat4> matrices) {
    lod.modelScale = playback.scale;
    walk_ = WalkModelWithMatrices(Nodes(), std::move(matrices), hiddenSwitches_, &lod);
}

void ModelInstance::ResetNodeState() {
    hiddenSwitches_.clear();
    visibleGunfire_.clear();
}

void ModelInstance::Pose(const Mat4& base) {
    Mat4 origin = base;
    if (holder_ != nullptr) {
        if (const Mat4* held = FindNodeMatrix(holder_->Nodes(), holder_->walk_, holderNode_)) {
            origin = holderRoll_ != 0.0f ? Rolled(*held, holderRoll_) : *held;
        }
    }

    lod.modelScale = playback.scale;

    AnimationPose pose = MakePose(playback);
    RootPlacement placement = MakePlacement(playback);

    std::vector<HeadAttachment> heads;
    if (headTree_ != nullptr) {
        heads.push_back({ headPlaceholder_, &headTree_->Nodes, headTree_->MatrixCount, headHiddenSwitches_ });
    }

    walk_ = WalkModel(Nodes(), tree_->MatrixCount, origin, heads, hiddenSwitches_,
                      playback.anim != nullptr ? &pose : nullptr, playback.hasRoot ? &placement : nullptr, &lod,
                      jointHook ? &jointHook : nullptr);
}

} // namespace Frigate::Model
