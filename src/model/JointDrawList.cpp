#include "model/JointDrawList.h"

#include <algorithm>

namespace Frigate {

namespace {

constexpr uint8_t kHeaderType = 0x01;
constexpr uint8_t kGroupType = 0x02;
constexpr uint8_t kGunfireType = 0x0c;
constexpr uint8_t kShadowType = 0x0d;
constexpr uint8_t kInterlinkType = 0x0f;
constexpr uint8_t kSimpleGroupType = 0x15;

bool OwnsMatrixIndex(uint8_t type) {
    return type == 0x01 || type == 0x02 || type == 0x03 || type == 0x15;
}

// The matrix of the nearest node at or above `index` that owns one
// (modelFindNodeMtx), or nullptr if none does.
const Mat4* FindNodeMatrix(const JointModel& model, int32_t index) {
    const std::vector<ModelNode>& nodes = *model.nodes;
    for (size_t hops = 0; index != kNoNode && hops <= nodes.size(); hops++) {
        const ModelNode& node = nodes[index];
        if (OwnsMatrixIndex(node.type) && node.matrix != kNoMatrix &&
            static_cast<size_t>(node.matrix) < model.walk->matrices.size()) {
            return &model.walk->matrices[node.matrix];
        }
        index = node.parent;
    }
    return nullptr;
}

// -(point . column 2 + translation z): the depth of `point` in the matrix's
// space, negated so that farther (more negative z) sorts larger.
float PointDepth(const Mat4& mtx, const float point[3]) {
    return -(point[0] * mtx.m[0][2] + point[1] * mtx.m[1][2] + point[2] * mtx.m[2][2] + mtx.m[3][2]);
}

} // namespace

bool StartsJointEntry(uint8_t nodeType) {
    return nodeType == kHeaderType || nodeType == kGroupType || nodeType == kGunfireType || nodeType == kShadowType ||
           nodeType == kInterlinkType || nodeType == kSimpleGroupType;
}

const ModelNode& VisitedModelNode(const JointModel& model, const VisitedNode& visited) {
    if (visited.attachment < 0) {
        return (*model.nodes)[visited.node];
    }
    return (*model.attachmentNodes[visited.attachment])[visited.node];
}

std::vector<JointEntry> BuildJointEntries(const std::vector<JointModel>& models) {
    std::vector<JointEntry> entries;
    for (size_t m = 0; m < models.size(); m++) {
        const std::vector<VisitedNode>& visited = models[m].walk->visited;
        for (size_t v = 0; v < visited.size(); v++) {
            if (StartsJointEntry(VisitedModelNode(models[m], visited[v]).type)) {
                entries.push_back({ m, v, 0.0f });
            }
        }
    }
    return entries;
}

void ComputeJointSortValues(std::vector<JointEntry>& entries, const std::vector<JointModel>& models) {
    for (JointEntry& entry : entries) {
        const JointModel& model = models[entry.model];
        const VisitedNode& visited = model.walk->visited[entry.visit];
        const ModelNode& node = VisitedModelNode(model, visited);
        const int32_t index = static_cast<int32_t>(visited.node);
        const Mat4* mtx = visited.attachment < 0 ? FindNodeMatrix(model, index) : nullptr;
        if (mtx == nullptr) {
            entry.sortValue = 0.0f;
            continue;
        }

        switch (node.type) {
            case kHeaderType:
            case kGroupType: {
                // Halfway between this joint and the group below it.
                const Mat4* other = node.sortOther != kNoNode ? FindNodeMatrix(model, node.sortOther) : nullptr;
                entry.sortValue = other != nullptr ? -(mtx->m[3][2] + other->m[3][2]) * 0.5f : -mtx->m[3][2];
                break;
            }
            case kSimpleGroupType:
                entry.sortValue = -mtx->m[3][2];
                break;
            case kGunfireType:
                entry.sortValue = PointDepth(*mtx, node.sortPoint);
                break;
            case kShadowType: {
                const float point[3] = { node.sortPoint[0], model.shadowHeight, node.sortPoint[2] };
                entry.sortValue = PointDepth(*mtx, point);
                break;
            }
            case kInterlinkType:
                entry.sortValue = std::min(PointDepth(*mtx, node.sortPoint), PointDepth(*mtx, node.sortPoint2));
                break;
            default:
                break;
        }
    }
}

void SortJointEntries(std::vector<JointEntry>& entries) {
    std::stable_sort(entries.begin(), entries.end(),
                     [](const JointEntry& a, const JointEntry& b) { return a.sortValue > b.sortValue; });
}

std::vector<JointEntry> BuildSortedJointEntries(const std::vector<JointModel>& models) {
    std::vector<JointEntry> entries = BuildJointEntries(models);
    ComputeJointSortValues(entries, models);
    SortJointEntries(entries);
    return entries;
}

std::vector<size_t> JointEntryNodes(const JointModel& model, size_t visit) {
    const std::vector<VisitedNode>& visited = model.walk->visited;
    std::vector<size_t> nodes;
    const uint32_t rootDepth = visited[visit].depth;

    size_t v = visit + 1;
    while (v < visited.size() && visited[v].depth > rootDepth) {
        if (StartsJointEntry(VisitedModelNode(model, visited[v]).type)) {
            // Another entry's joint: it and what is below it are not drawn here.
            const uint32_t jointDepth = visited[v].depth;
            v++;
            while (v < visited.size() && visited[v].depth > jointDepth) {
                v++;
            }
            continue;
        }
        nodes.push_back(v);
        v++;
    }
    return nodes;
}

} // namespace Frigate
