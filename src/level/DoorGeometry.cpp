#include "level/DoorGeometry.h"

#include <cstring>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/Vertex.h"
#include "level/ClippedDoor.h"
#include "level/Matrix.h"
#include "resource/type/ModelTree.h"
#include "ship/resource/ResourceManager.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.28318530717958647692f;
// The node type of a display list that holds collision vertices.
constexpr uint8_t kCollisionNodeType = 0x18;

Mtxf ToMtxf(const Mat4& in) {
    Mtxf out{};
    std::memcpy(out.m, in.m, sizeof(out.m));
    return out;
}

Mat4 ToMat4(const Mtxf& in) {
    Mat4 out{};
    std::memcpy(out.m, in.m, sizeof(out.m));
    return out;
}

// A leaf of the door: `rotation` with the origin of the node in switch slot
// `slot` as its position, under `parent`.
Mat4 Leaf(const ModelTree& tree, size_t slot, Mtxf rotation, const Mat4& parent) {
    const float* origin = tree.Nodes[static_cast<size_t>(tree.Switches[slot])].origin;
    for (size_t axis = 0; axis < 3; axis++) {
        rotation.m[3][axis] = origin[axis];
    }
    return ToMat4(Multiply(ToMtxf(parent), rotation));
}

// The two lids of skeleton_eyelid_door swing opposite ways about X.
std::vector<Mat4> EyelidMatrices(const ModelTree& tree, const Mat4& base, const PlacedDoor& door) {
    std::vector<Mat4> matrices(tree.MatrixCount, base);
    if (tree.Switches.size() < 3) {
        return matrices;
    }
    const float angle = kTau - ((door.openPosition * kTau) / 360.0f);
    matrices[1] = Leaf(tree, 1, RotationX(angle), base);
    matrices[2] = Leaf(tree, 2, RotationX(kTau - angle), base);
    return matrices;
}

// skeleton_iris_door: six blades, each a pair of nested leaves turning about Z.
// The outer leaf turns with the opening; the inner one only once it is past
// 30% of the way.
std::vector<Mat4> IrisMatrices(const ModelTree& tree, const Mat4& base, const PlacedDoor& door) {
    constexpr size_t kBlades = 6;
    std::vector<Mat4> matrices(tree.MatrixCount, base);
    if (tree.Switches.size() < (kBlades * 2) + 1) {
        return matrices;
    }
    float inner = 0.0f;
    const float outer = (door.openPosition * kTau) / 360.0f;
    const float threshold = door.maxFrac * 0.3f;
    if (threshold < door.openPosition) {
        inner = (((door.maxFrac * (door.openPosition - threshold)) / (door.maxFrac - threshold)) * kTau) / 360.0f;
    }
    for (size_t blade = 0; blade < kBlades; blade++) {
        const size_t first = (blade * 2) + 1;
        matrices[first] = Leaf(tree, first, RotationZ(inner), base);
        matrices[first + 1] = Leaf(tree, first + 1, RotationZ(outer), matrices[first]);
    }
    return matrices;
}

} // namespace

void FillDoorMatrices(const PropModel& model, const PlacedDoor& door, const Mat4& base,
                      Model::ModelDrawOverrides* overrides) {
    if (model.skeleton == "eyelid_door") {
        overrides->matrices = [base, &door](const ModelTree& tree) { return EyelidMatrices(tree, base, door); };
    } else if (model.skeleton == "iris_door") {
        overrides->matrices = [base, &door](const ModelTree& tree) { return IrisMatrices(tree, base, door); };
    }
}

void FillDoorOverrides(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const std::string& modelPath,
                       const PropModel& model, const PlacedDoor& door, const Mat4& base,
                       Model::ModelDrawOverrides* overrides) {
    FillDoorMatrices(model, door, base, overrides);

    if ((door.doorFlags & DoorFlag::ClipToBbox) == 0) {
        return;
    }
    const auto tree =
        std::dynamic_pointer_cast<ModelTree>(resourceManager->LoadResource(CRC64((modelPath + "/tree").c_str())));
    if (tree == nullptr || tree->Nodes.empty() || tree->Nodes[0].child == kNoNode) {
        return;
    }
    // RootNode->Child->Child: the collision list under the box node.
    const int32_t box = tree->Nodes[0].child;
    const int32_t collision = tree->Nodes[static_cast<size_t>(box)].child;
    if (collision == kNoNode || tree->Nodes[static_cast<size_t>(collision)].type != kCollisionNodeType) {
        return;
    }
    const uint32_t offset = tree->Nodes[static_cast<size_t>(collision)].offset;
    const auto source = std::dynamic_pointer_cast<Fast::Vertex>(
        resourceManager->LoadResource(CRC64(fmt::format("{}/vtx/{:06x}", modelPath, offset).c_str())));
    if (source == nullptr || source->VertexList.empty()) {
        spdlog::error("door: no vertices for the collision node at {:06x} of '{}'", offset, modelPath);
        return;
    }

    static_assert(sizeof(Vtx_t) == sizeof(ModelVertex), "a model vertex is a Vtx_t");
    const bool vertical = door.doorType == DoorMotionType::Vertical;
    const std::vector<ModelVertex> clipped =
        ClipDoorVertices(reinterpret_cast<const ModelVertex*>(&source->VertexList[0].v), source->VertexList.size(),
                         vertical, DoorClipCutoff(door.bbox, vertical));
    Model::NodeVertices replaced;
    replaced.node = offset;
    replaced.vertices.resize(clipped.size());
    std::memcpy(replaced.vertices.data(), clipped.data(), clipped.size() * sizeof(Vtx_t));
    overrides->vertices.push_back(std::move(replaced));
}

} // namespace Frigate::Level
