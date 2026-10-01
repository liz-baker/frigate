#include "level/ModelHit.h"

#include <cstring>

#include "level/LightFixtures.h"
#include "level/TriangleList.h"

namespace Frigate::Level {

namespace {

constexpr uint8_t kBoundingBoxType = 0x0A;
constexpr uint8_t kHitSphereType = 0x11;
constexpr uint8_t kDisplayListType = 0x04;
constexpr uint8_t kCollisionListType = 0x18;
constexpr uint8_t kHeaderType = 0x01;
constexpr uint8_t kGroupType = 0x02;
constexpr uint8_t kGroupAltType = 0x03;
constexpr uint8_t kSimpleGroupType = 0x15;

const Mtxf& AsMtxf(const Mat4& matrix) {
    static_assert(sizeof(Mat4) == sizeof(Mtxf), "Mat4 and Mtxf are both 4x4 floats");
    return *reinterpret_cast<const Mtxf*>(&matrix);
}

const std::vector<ModelNode>& NodesOf(const NodeRef& ref) {
    return ref.attachment < 0 ? ref.model->Nodes() : *ref.model->HeadNodes();
}

// modelTestRayIntersectsNodeBBox and sub_GAME_7F074CAC: whether the ray meets a
// hit node of a posed model.
bool RayMeetsHitNode(const NodeRef& ref, const Vec3& origin, const Vec3& dir) {
    const ModelNode& node = NodeOf(ref);
    const Mtxf* matrix = NodeMatrix(ref);
    if (matrix == nullptr) {
        return false;
    }
    if (node.type == kBoundingBoxType) {
        return RayIntersectsTransformedBox(node.bounds, *matrix, origin, dir);
    }
    HitSphere sphere;
    sphere.flags = node.hitFlags;
    sphere.radiusSquared = node.hitRadiusSquared;
    sphere.centre = { node.hitCentre[0], node.hitCentre[1], node.hitCentre[2] };
    sphere.scale1 = node.hitScale1;
    sphere.scale2 = node.hitScale2;
    const Mtxf* other = nullptr;
    if (node.hitOther != kNoNode) {
        NodeRef otherRef = ref;
        otherRef.node = node.hitOther;
        other = NodeMatrix(otherRef);
    }
    return RayIntersectsHitSphere(sphere, *matrix, other, origin, dir);
}

bool IsHitNode(const ModelNode& node) {
    return (node.type == kBoundingBoxType || node.type == kHitSphereType) && node.hasHit;
}

// The triangles of one list of a display-list node against a ray in the node's
// space (bgTestHitOnObj's loop over a list). `best` is the squared distance
// of the nearest hit so far.
bool TestList(const Gfx* commands, size_t count, const Vtx* vertices, size_t vertexCount, const Vec3& from,
              const Vec3& to, const Vec3& dir, const TextureHits* textures, float* best, HitThing* hit) {
    bool found = false;
    uint64_t imageHash = 0;
    const Vtx* batch = nullptr;
    uint32_t firstSlot = 0;
    uint32_t batchCount = 0;
    const Vec3 offset = { 0.0f, 0.0f, 0.0f };

    for (size_t i = 0; i < count && !IsEndOfList(commands[i]); i += CommandWidth(commands[i])) {
        const Gfx& command = commands[i];
        if (IsVertexLoad(command)) {
            const VertexLoad load = DecodeVertexLoad(command);
            // The load's operand is an offset into the node's vertices, or the
            // address it was resolved to the first time the interpreter ran it.
            const uintptr_t operand = load.operand;
            const auto* start = reinterpret_cast<const char*>(vertices);
            batch = reinterpret_cast<const Vtx*>(operand > 0xFFFFF ? reinterpret_cast<const char*>(operand) : start + operand);
            firstSlot = load.firstSlot;
            batchCount = load.count;
            continue;
        }
        if (IsImageLoad(command) && i + 1 < count) {
            imageHash = OperandHash(commands[i + 1]);
            continue;
        }
        if (!IsTriangle(command) || batch == nullptr) {
            continue;
        }

        const int types = Opcode(command) == static_cast<uint8_t>(Fast::F3DEX_G_TRI1) ? 1 : 4;
        for (int t = 0; t < types; t++) {
            const int16_t type = types == 1 ? 0 : static_cast<int16_t>(t + 1);
            int slot[3];
            TriangleSlots(&command, static_cast<uint32_t>(type), slot);
            int idx[3];
            for (int k = 0; k < 3; k++) {
                idx[k] = slot[k] - static_cast<int>(firstSlot);
            }
            if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || static_cast<uint32_t>(idx[0]) >= batchCount ||
                static_cast<uint32_t>(idx[1]) >= batchCount || static_cast<uint32_t>(idx[2]) >= batchCount) {
                continue;
            }

            int32_t bboxMin[3] = { 0x7FFF, 0x7FFF, 0x7FFF };
            int32_t bboxMax[3] = { -0x8000, -0x8000, -0x8000 };
            for (int k = 0; k < 3; k++) {
                const Vtx& v = batch[idx[k]];
                for (int axis = 0; axis < 3; axis++) {
                    if (v.v.ob[axis] < bboxMin[axis]) {
                        bboxMin[axis] = v.v.ob[axis];
                    }
                    if (bboxMax[axis] < v.v.ob[axis]) {
                        bboxMax[axis] = v.v.ob[axis];
                    }
                }
            }
            if (!RayIntersectsBox(from, dir, bboxMin, bboxMax)) {
                continue;
            }

            TriangleHit triangle;
            if (!RayIntersectsTriangle(batch[idx[0]].v.ob, batch[idx[1]].v.ob, batch[idx[2]].v.ob, offset, from, to,
                                       dir, &triangle)) {
                continue;
            }
            const float dx = static_cast<float>(static_cast<int32_t>(triangle.position[0]) - static_cast<int32_t>(from[0]));
            const float dy = static_cast<float>(static_cast<int32_t>(triangle.position[1]) - static_cast<int32_t>(from[1]));
            const float dz = static_cast<float>(static_cast<int32_t>(triangle.position[2]) - static_cast<int32_t>(from[2]));
            const float d = ((dx * dx) + (dy * dy)) + (dz * dz);
            if (d < *best) {
                *best = d;
                hit->hitpos = triangle.position;
                hit->normal = triangle.normal;
                hit->triCommand = &command;
                hit->triType = type;
                hit->textureHash = imageHash;
                hit->texture = textures != nullptr && imageHash != 0 ? textures->NumberOf(imageHash) : -1;
                found = true;
            }
        }
    }
    (void)vertexCount;
    return found;
}

} // namespace

const ModelNode& NodeOf(const NodeRef& ref) {
    return NodesOf(ref)[static_cast<size_t>(ref.node)];
}

const Mtxf* NodeMatrix(const NodeRef& ref) {
    const Mat4* matrix =
        ref.attachment < 0
            ? FindNodeMatrix(ref.model->Nodes(), ref.model->Walk(), ref.node)
            : FindNodeMatrix(*ref.model->HeadNodes(), ref.model->Walk().attachments[static_cast<size_t>(ref.attachment)],
                             ref.node);
    return matrix != nullptr ? &AsMtxf(*matrix) : nullptr;
}

int NodeMatrixIndex(const Model::ModelInstance& model, int32_t node) {
    const Mat4* matrix = FindNodeMatrix(model.Nodes(), model.Walk(), node);
    if (matrix == nullptr) {
        return -1;
    }
    return static_cast<int>(matrix - model.Walk().matrices.data());
}

float JointSize(const NodeRef& ref) {
    const std::vector<ModelNode>& nodes = NodesOf(ref);
    int32_t index = ref.node;
    for (size_t hops = 0; index != kNoNode && hops <= nodes.size(); hops++) {
        const ModelNode& node = nodes[static_cast<size_t>(index)];
        if (node.type == kHeaderType || node.type == kGroupType || node.type == kGroupAltType ||
            node.type == kSimpleGroupType) {
            return node.jointRadius * ref.model->playback.scale;
        }
        index = node.parent;
    }
    return 0.0f;
}

float InstanceSize(const Model::ModelInstance& model) {
    return model.Tree().BoundingRadius * model.playback.scale;
}

int FindProjectileHitCandidate(const Model::ModelInstance& model, const Vec3& origin, const Vec3& dir, int32_t* nodeIndex) {
    // The nodes the model's posed state reaches (a hidden switch's child and a
    // level of detail out of range are not), in the order the game walks them.
    const std::vector<VisitedNode>& visited = model.Walk().visited;
    const std::vector<ModelNode>& nodes = model.Nodes();

    // The root is never itself a candidate; a walk that returned a node goes on
    // with what follows it.
    size_t at = 1;
    if (*nodeIndex != kNoNode) {
        for (size_t i = 0; i < visited.size(); i++) {
            if (visited[i].attachment < 0 && static_cast<int32_t>(visited[i].node) == *nodeIndex) {
                at = i + 1;
                break;
            }
        }
        *nodeIndex = kNoNode;
    }

    for (; at < visited.size(); at++) {
        if (visited[at].attachment >= 0) {
            continue;
        }
        const ModelNode& current = nodes[visited[at].node];
        if (!IsHitNode(current)) {
            continue;
        }
        NodeRef ref;
        ref.model = &model;
        ref.node = static_cast<int32_t>(visited[at].node);
        if (RayMeetsHitNode(ref, origin, dir)) {
            *nodeIndex = ref.node;
            return current.hitPart;
        }
        // A hit node the ray misses takes what is below it with it.
        const uint32_t depth = visited[at].depth;
        while (at + 1 < visited.size() && visited[at + 1].depth > depth) {
            at++;
        }
    }
    return 0;
}

bool FindTriangleHit(const Model::ModelInstance& model, int32_t startNode, const Vec3& origin, const Vec3& dir,
                     ModelGeometrySource& geometry, const TextureHits* textures, HitThing* hit, int* matrixIndex,
                     int32_t* hitNode) {
    const std::vector<ModelNode>& nodes = model.Nodes();
    const std::vector<VisitedNode>& visited = model.Walk().visited;
    const Mtxf* currentMatrix = nullptr;
    Vec3 from{};
    Vec3 to{};
    Vec3 localDir{};
    float best = 4294967295.0f;
    bool found = false;

    // The node and what is below it, among the nodes the model's state reaches.
    size_t at = 0;
    while (at < visited.size() &&
           !(visited[at].attachment < 0 && static_cast<int32_t>(visited[at].node) == startNode)) {
        at++;
    }
    if (at == visited.size()) {
        return false;
    }
    const uint32_t startDepth = visited[at].depth;

    const size_t first = at;
    for (; at < visited.size() && !found; at++) {
        if (at != first && visited[at].depth <= startDepth) {
            break;
        }
        if (visited[at].attachment >= 0) {
            continue;
        }
        const int32_t node = static_cast<int32_t>(visited[at].node);
        const ModelNode& current = nodes[static_cast<size_t>(node)];
        std::shared_ptr<const NodeGeometry> lists;
        if ((current.type == kDisplayListType || current.type == kCollisionListType) &&
            current.primaryList != kNoList) {
            lists = geometry.LoadNode(model.Path(), current);
        }

        if (lists != nullptr && lists->primary != nullptr) {
            const int index = NodeMatrixIndex(model, node);
            NodeRef ref;
            ref.model = &model;
            ref.node = node;
            const Mtxf* matrix = index >= 0 ? NodeMatrix(ref) : nullptr;
            if (matrix != nullptr && matrix != currentMatrix) {
                currentMatrix = matrix;
                const Mtxf inverse = InvertAffine(*matrix);
                from = TransformVector(inverse, origin);
                localDir = RotateVector(inverse, dir);
                to = { localDir[0] * 32767.0f + from[0], localDir[1] * 32767.0f + from[1], localDir[2] * 32767.0f + from[2] };
            }

            bool hitList = TestList(lists->primary, lists->primaryCount, lists->vertices, lists->vertexCount, from, to,
                                    localDir, textures, &best, hit);
            if (lists->secondary != nullptr) {
                hitList = TestList(lists->secondary, lists->secondaryCount, lists->vertices, lists->vertexCount, from,
                                   to, localDir, textures, &best, hit) ||
                          hitList;
            }
            if (hitList) {
                *matrixIndex = index;
                *hitNode = node;
                found = true;
            }
        }
    }
    return found;
}

JointHitSearch::JointHitSearch(const std::vector<const Model::ModelInstance*>& models) : models_(models) {
    jointModels_.reserve(models.size());
    for (const Model::ModelInstance* model : models) {
        jointModels_.push_back(Model::MakeJointModel(*model));
    }
    entries_ = BuildSortedJointEntries(jointModels_);
    entry_ = static_cast<int>(entries_.size()) - 1;
}

int JointHitSearch::First(const Vec3& origin, const Vec3& dir, NodeRef* hit) {
    entry_ = static_cast<int>(entries_.size()) - 1;
    resuming_ = false;
    return Next(origin, dir, hit);
}

int JointHitSearch::Next(const Vec3& origin, const Vec3& dir, NodeRef* hit) {
    for (; entry_ >= 0; entry_--, resuming_ = false) {
        const JointEntry& entry = entries_[static_cast<size_t>(entry_)];
        const JointModel& jointModel = jointModels_[entry.model];
        const std::vector<VisitedNode>& visited = jointModel.walk->visited;
        const uint32_t rootDepth = visited[entry.visit].depth;

        size_t v = resuming_ ? resume_ : entry.visit + 1;
        resuming_ = false;
        while (v < visited.size() && visited[v].depth > rootDepth) {
            const ModelNode& node = VisitedModelNode(jointModel, visited[v]);
            const uint32_t depth = visited[v].depth;
            bool descend = true;

            if (StartsJointEntry(node.type)) {
                // Another entry's joint: it and what is below it are tested there.
                descend = false;
            } else if (IsHitNode(node)) {
                NodeRef ref;
                ref.model = models_[entry.model];
                ref.node = static_cast<int32_t>(visited[v].node);
                ref.attachment = visited[v].attachment;
                if (RayMeetsHitNode(ref, origin, dir)) {
                    *hit = ref;
                    resume_ = v + 1;
                    resuming_ = true;
                    return node.hitPart;
                }
                descend = false;
            }

            v++;
            if (!descend) {
                while (v < visited.size() && visited[v].depth > depth) {
                    v++;
                }
            }
        }
    }
    return 0;
}

} // namespace Frigate::Level
