#include "model/ModelWalk.h"

#include <cmath>
#include <cstddef>

namespace Frigate {

namespace {

// Node types that own a matrix: header, the two group kinds, simple group.
bool OwnsMatrix(uint8_t type) {
    return type == 0x01 || type == 0x02 || type == 0x03 || type == 0x15;
}

// Only these two group kinds carry a JointID (ModelNode.h), so only they are
// ever animated.
bool IsGroupNodeType(uint8_t type) {
    return type == 0x02 || type == 0x03;
}

Mat4 Translated(const float origin[3], const Mat4& parent) {
    // translate(origin) * parent: only row 3 of the result differs from the parent's.
    Mat4 out = parent;
    for (int col = 0; col < 3; col++) {
        out.m[3][col] += origin[0] * parent.m[0][col] + origin[1] * parent.m[1][col] + origin[2] * parent.m[2][col];
    }
    return out;
}

// rotate(rot) with position set to origin (untransformed by the rotation),
// then composed as local * parent -- the general form Translated above is
// the identity-rotation special case of. Transcribed from the GoldenEye
// decomp's `matrix_4x4_set_rotation_around_xyz` (rotation) and
// `matrix_4x4_multiply_homogeneous` (composition order).
Mat4 RotatedAndTranslated(const JointRotation& rot, const float origin[3], const Mat4& parent) {
    float cx = std::cos(rot.x), sx = std::sin(rot.x);
    float cy = std::cos(rot.y), sy = std::sin(rot.y);
    float cz = std::cos(rot.z), sz = std::sin(rot.z);
    float sxsz = sx * sz, cxsz = cx * sz, sxcz = sx * cz, cxcz = cx * cz;

    Mat4 local{};
    local.m[0][0] = cy * cz;
    local.m[0][1] = cy * sz;
    local.m[0][2] = -sy;
    local.m[1][0] = sxcz * sy - cxsz;
    local.m[1][1] = sxsz * sy + cxcz;
    local.m[1][2] = sx * cy;
    local.m[2][0] = cxcz * sy + sxsz;
    local.m[2][1] = cxsz * sy - sxcz;
    local.m[2][2] = cx * cy;
    local.m[3][0] = origin[0];
    local.m[3][1] = origin[1];
    local.m[3][2] = origin[2];

    Mat4 out{};
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            out.m[row][col] = local.m[row][0] * parent.m[0][col] + local.m[row][1] * parent.m[1][col] +
                              local.m[row][2] * parent.m[2][col];
        }
    }
    for (int col = 0; col < 3; col++) {
        out.m[3][col] += parent.m[3][col];
    }
    out.m[3][3] = 1.0f;
    return out;
}

struct Walker {
    const std::vector<ModelNode>& nodes;
    ModelWalk& out;
    const Mat4& base;
    const std::vector<HeadAttachment>& headAttachments;
    const std::vector<uint32_t>& visibleSwitches;
    const AnimationPose* pose;

    void Store(int16_t index, const Mat4& mtx) {
        if (index >= 0 && static_cast<size_t>(index) < out.matrices.size()) {
            out.matrices[index] = mtx;
        }
    }

    const HeadAttachment* FindAttachment(uint32_t placeholderOffset) const {
        for (const HeadAttachment& attachment : headAttachments) {
            if (attachment.placeholderOffset == placeholderOffset) {
                return &attachment;
            }
        }
        return nullptr;
    }

    bool IsSwitchedOn(uint32_t offset) const {
        for (uint32_t visible : visibleSwitches) {
            if (visible == offset) {
                return true;
            }
        }
        return false;
    }

    // own's translation is the node's position in view space (base already
    // includes the camera), so its magnitude is the render distance.
    bool InLodRange(const ModelNode& node, const Mat4& own) const {
        float dx = own.m[3][0], dy = own.m[3][1], dz = own.m[3][2];
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        return (distance > node.lodMinDistance || node.lodMinDistance == 0.0f) && distance <= node.lodMaxDistance;
    }

    // The plane vector rotated (mode 0, the full 3x3) or scaled by one
    // matrix row (mode 1/2/3, by the vector's x/y/z component), and the
    // plane point transformed by the node's own inherited matrix (own,
    // translation included). Both row-vector transforms, matching
    // Translated above.
    bool BspChildFirst(const ModelNode& node, const Mat4& own) const {
        const float* v = node.bspVector;
        float tv[3];
        if (node.bspTransformMode >= 1 && node.bspTransformMode <= 3) {
            int row = node.bspTransformMode - 1;
            for (int col = 0; col < 3; col++) {
                tv[col] = v[row] * own.m[row][col];
            }
        } else {
            for (int col = 0; col < 3; col++) {
                tv[col] = v[0] * own.m[0][col] + v[1] * own.m[1][col] + v[2] * own.m[2][col];
            }
        }
        const float* p = node.bspPoint;
        float tp[3];
        for (int col = 0; col < 3; col++) {
            tp[col] = p[0] * own.m[0][col] + p[1] * own.m[1][col] + p[2] * own.m[2][col] + own.m[3][col];
        }
        float dot = tv[0] * tp[0] + tv[1] * tp[1] + tv[2] * tp[2];
        return dot < 0.0f;
    }

    // A group node's rest-pose matrix (`Translated`), unless `pose` names a
    // clip and a joint-table entry for this node's jointId -- then its
    // sampled rotation is applied first.
    Mat4 OwnMatrix(const ModelNode& node, const Mat4& parent) const {
        if (pose != nullptr && pose->clip != nullptr && pose->joints != nullptr && IsGroupNodeType(node.type) &&
            node.jointId < pose->joints->size()) {
            const JointChannels& channels = (*pose->joints)[node.jointId];
            uint16_t channel = pose->mirror ? channels.mtxB : channels.mtxA;
            JointRotation rotation = SampleJointRotation(*pose->clip, channel, pose->frame, pose->mirror);
            return RotatedAndTranslated(rotation, node.origin, parent);
        }
        return Translated(node.origin, parent);
    }

    // A malformed tree (a link that revisits a node) is bounded by the node
    // count so the walk always terminates. `until` stops a BSP child
    // group's traversal at the other group's own head instead of the end
    // of the sibling chain.
    void Visit(int32_t first, const Mat4& parent, size_t depth, int32_t until = kNoNode) {
        size_t visited = 0;
        for (int32_t i = first; i != kNoNode && i != until && visited < nodes.size(); i = nodes[i].next, visited++) {
            const ModelNode& node = nodes[i];
            Mat4 own = parent;
            if (OwnsMatrix(node.type)) {
                own = OwnMatrix(node, parent);
                Store(node.matrix, own);
                Store(node.bendMatrix, own);
            }
            if (node.type == kHeadPlaceholderNodeType) {
                if (const HeadAttachment* attachment = FindAttachment(node.offset)) {
                    out.attachments.push_back(WalkModel(*attachment->nodes, attachment->matrixCount, own));
                }
            }
            if (node.primaryList != kNoList) {
                out.lists.push_back({ node.offset, node.primaryList });
            }
            if (node.secondaryList != kNoList) {
                out.lists.push_back({ node.offset, node.secondaryList });
            }
            bool descend = true;
            if (node.type == kSwitchNodeType) {
                descend = IsSwitchedOn(node.offset);
            } else if (node.type == kLodNodeType) {
                descend = InLodRange(node, own);
            }
            if (depth < nodes.size() && descend) {
                if (node.type == kBspNodeType) {
                    bool childFirst = BspChildFirst(node, own);
                    int32_t group1 = childFirst ? node.child : node.bspSecondChild;
                    int32_t group2 = childFirst ? node.bspSecondChild : node.child;
                    Visit(group1, own, depth + 1, group2);
                    if (group2 != kNoNode) {
                        Visit(group2, own, depth + 1);
                    }
                } else {
                    Visit(node.child, own, depth + 1);
                }
            }
        }
    }
};

} // namespace

Mat4 IdentityMat4() {
    Mat4 out{};
    for (int i = 0; i < 4; i++) {
        out.m[i][i] = 1.0f;
    }
    return out;
}

ModelWalk WalkModel(const std::vector<ModelNode>& nodes, uint32_t matrixCount, const Mat4& base,
                    const std::vector<HeadAttachment>& headAttachments, const std::vector<uint32_t>& visibleSwitches,
                    const AnimationPose* pose) {
    ModelWalk result;
    result.matrices.assign(matrixCount > 0 ? matrixCount : 1, base);
    if (!nodes.empty()) {
        Walker{ nodes, result, base, headAttachments, visibleSwitches, pose }.Visit(0, base, 0);
    }
    return result;
}

} // namespace Frigate
