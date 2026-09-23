#include "model/ModelWalk.h"

#include <cmath>
#include <cstdio>

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

ModelNode Group(uint32_t offset, int16_t matrix, float x, float y, float z, int32_t child, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = 0x02;
    node.matrix = matrix;
    node.origin[0] = x;
    node.origin[1] = y;
    node.origin[2] = z;
    node.child = child;
    node.next = next;
    return node;
}

ModelNode Draws(uint32_t offset, uint32_t primary, uint32_t secondary, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = 0x04;
    node.primaryList = primary;
    node.secondaryList = secondary;
    node.next = next;
    return node;
}

ModelNode HeadPlaceholder(uint32_t offset, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = kHeadPlaceholderNodeType;
    node.next = next;
    return node;
}

ModelNode Switch(uint32_t offset, int32_t child, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = kSwitchNodeType;
    node.child = child;
    node.next = next;
    return node;
}

ModelNode Lod(uint32_t offset, float minDistance, float maxDistance, int32_t child, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = kLodNodeType;
    node.lodMinDistance = minDistance;
    node.lodMaxDistance = maxDistance;
    node.child = child;
    node.next = next;
    return node;
}

ModelNode Bsp(uint32_t offset, float pointX, float vectorX, int32_t child, int32_t secondChild, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = kBspNodeType;
    node.bspPoint[0] = pointX;
    node.bspVector[0] = vectorX;
    node.bspTransformMode = 0;
    node.child = child;
    node.bspSecondChild = secondChild;
    node.next = next;
    return node;
}

} // namespace

int main() {
    // group 0 at (1,2,3) -> [group 1 at (10,0,0) -> draw A, draw B]
    std::vector<ModelNode> nodes = {
        Group(0x00, 0, 1, 2, 3, 1, kNoNode),
        Group(0x18, 1, 10, 0, 0, 2, 3),
        Draws(0x30, 0x100, 0x140, kNoNode),
        Draws(0x48, 0x200, kNoList, kNoNode),
    };
    nodes[3].parent = 0;

    Mat4 base = IdentityMat4();
    base.m[3][2] = -5.0f;
    ModelWalk walk = WalkModel(nodes, 3, base);

    Check(walk.matrices.size() == 3, "matrix count");
    Check(walk.matrices[0].m[3][0] == 1 && walk.matrices[0].m[3][2] == -2, "root matrix is origin plus base");
    Check(walk.matrices[1].m[3][0] == 11 && walk.matrices[1].m[3][1] == 2, "child accumulates the parent's origin");
    Check(walk.matrices[2].m[3][2] == -5, "an unowned matrix stays the base");
    Check(walk.lists.size() == 3, "list count");
    Check(walk.lists[0].node == 0x30 && walk.lists[0].list == 0x100 && walk.lists[1].list == 0x140 &&
              walk.lists[2].node == 0x48 && walk.lists[2].list == 0x200,
          "lists come out primary, secondary, then the next sibling");

    // A parent's rotation carries into the child's translation.
    Mat4 turned = IdentityMat4();
    turned.m[0][0] = 0, turned.m[0][1] = 1, turned.m[1][0] = -1, turned.m[1][1] = 0;
    std::vector<ModelNode> pair = { Group(0, 0, 0, 0, 0, 1, kNoNode), Group(0x18, 1, 1, 0, 0, kNoNode, kNoNode) };
    ModelWalk rotated = WalkModel(pair, 2, turned);
    Check(rotated.matrices[1].m[3][0] == 0 && rotated.matrices[1].m[3][1] == 1, "translation is rotated by the parent");

    // A link cycle must not hang the walk.
    std::vector<ModelNode> loop = { Group(0, 0, 0, 0, 0, 0, 0) };
    WalkModel(loop, 1, base);

    Check(WalkModel({}, 0, base).matrices.size() == 1, "an empty tree still yields one base matrix");

    // A body's head placeholder with a matching HeadAttachment walks the
    // head's tree with the placeholder's inherited matrix as base, and the
    // result lands in attachments rather than the body's own fields.
    {
        std::vector<ModelNode> body = { Group(0x00, 0, 5, 0, 0, 1, kNoNode), HeadPlaceholder(0x18, kNoNode) };
        std::vector<ModelNode> head = { Draws(0x00, 0x100, kNoList, kNoNode) };
        std::vector<HeadAttachment> attachments = { { 0x18, &head, 1 } };

        ModelWalk withAttachment = WalkModel(body, 1, base, attachments);
        Check(withAttachment.attachments.size() == 1, "a matching placeholder yields one attachment");
        Check(withAttachment.lists.empty(), "the head's lists are not folded into the body's own list");
        Check(withAttachment.attachments[0].matrices.size() == 1 &&
                  withAttachment.attachments[0].matrices[0].m[3][0] == 5,
              "the head's matrix 0 is the placeholder's inherited matrix, not the base");
        Check(withAttachment.attachments[0].lists.size() == 1 && withAttachment.attachments[0].lists[0].list == 0x100,
              "the head's own lists come out in its attachment");

        ModelWalk withoutAttachment = WalkModel(body, 1, base);
        Check(withoutAttachment.attachments.empty(), "no HeadAttachment means no attachment is resolved");
    }

    // A switch node's child is only visited when its offset is in
    // visibleSwitches; otherwise it's off by default, drawing nothing.
    {
        std::vector<ModelNode> withSwitch = { Switch(0x00, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };

        ModelWalk off = WalkModel(withSwitch, 1, base);
        Check(off.lists.empty(), "an unlisted switch is off by default");

        ModelWalk on = WalkModel(withSwitch, 1, base, {}, { 0x00 });
        Check(on.lists.size() == 1 && on.lists[0].list == 0x100, "a listed switch draws its child");
    }

    // An LOD node's child is visited only when the node's own inherited
    // translation distance falls in [lodMinDistance, lodMaxDistance).
    {
        Mat4 far = IdentityMat4();
        far.m[3][2] = -50.0f; // distance 50

        std::vector<ModelNode> inRange = { Lod(0x00, 10.0f, 100.0f, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };
        Check(WalkModel(inRange, 1, far).lists.size() == 1, "distance inside [min, max) draws the child");

        std::vector<ModelNode> tooNear = { Lod(0x00, 60.0f, 100.0f, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };
        Check(WalkModel(tooNear, 1, far).lists.empty(), "distance below min skips the child");

        std::vector<ModelNode> tooFar = { Lod(0x00, 10.0f, 40.0f, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };
        Check(WalkModel(tooFar, 1, far).lists.empty(), "distance at or beyond max skips the child");

        // A zero min distance means no lower bound, matching the decomp's
        // "MinDistance == 0" special case.
        std::vector<ModelNode> zeroMin = { Lod(0x00, 0.0f, 100.0f, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };
        Check(WalkModel(zeroMin, 1, IdentityMat4()).lists.size() == 1,
              "a zero min distance draws the child even at distance zero");
    }

    // A BSP node's two child groups both draw, but a negative plane-point
    // dot plane-vector (mode 0: rotate the vector by the node's own
    // identity matrix, i.e. use it as-is) visits the child-link group
    // first; a non-negative one visits the second group first.
    {
        std::vector<ModelNode> back = { Bsp(0x00, -1.0f, 1.0f, 1, 2, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode),
                                        Draws(0x30, 0x200, kNoList, kNoNode) };
        ModelWalk behind = WalkModel(back, 1, IdentityMat4());
        Check(behind.lists.size() == 2 && behind.lists[0].list == 0x100 && behind.lists[1].list == 0x200,
              "a negative dot product visits the child-link group first");

        std::vector<ModelNode> front = { Bsp(0x00, 1.0f, 1.0f, 1, 2, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode),
                                         Draws(0x30, 0x200, kNoList, kNoNode) };
        ModelWalk ahead = WalkModel(front, 1, IdentityMat4());
        Check(ahead.lists.size() == 2 && ahead.lists[0].list == 0x200 && ahead.lists[1].list == 0x100,
              "a non-negative dot product visits the second group first");

        // No second group: the child-link group alone still draws.
        std::vector<ModelNode> single = { Bsp(0x00, 1.0f, 1.0f, 1, kNoNode, kNoNode),
                                          Draws(0x18, 0x100, kNoList, kNoNode) };
        ModelWalk solo = WalkModel(single, 1, IdentityMat4());
        Check(solo.lists.size() == 1 && solo.lists[0].list == 0x100,
              "a BSP node with no second group still draws its child");
    }

    // A group node's JointID, resolved through the pose's joint table to an
    // animation channel, rotates its matrix before the origin translation;
    // a 90-degree Z rotation maps the node's local X axis onto world Y.
    {
        std::vector<ModelNode> nodes = { Group(0x00, 0, 0, 0, 0, kNoNode, kNoNode) };
        nodes[0].jointId = 0;

        // One frame, 16-bit angles (no quantization loss): X=0, Y=0, Z=0x4000
        // (= pi/2 once scaled by 2*pi/65536), channel 0's bit offset 0.
        uint8_t frame[6] = { 0x00, 0x00, 0x00, 0x00, 0x40, 0x00 };
        AnimationClip clip;
        clip.angleWidthBits = 16;
        clip.frameSizeBytes = sizeof(frame);
        clip.bitstream = frame;
        clip.frameCount = 1;
        std::vector<JointChannels> joints = { { 0, 0 } };
        AnimationPose pose{ &clip, &joints, 0.0f, false };

        ModelWalk animated = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(animated.matrices[0].m[0][0], 0.0f) && Near(animated.matrices[0].m[0][1], 1.0f),
              "a 90-degree Z rotation maps the local X axis onto world Y");

        ModelWalk rest = WalkModel(nodes, 1, IdentityMat4());
        Check(rest.matrices[0].m[0][0] == 1.0f && rest.matrices[0].m[0][1] == 0.0f,
              "the same node with no pose stays at rest pose");

        // A pose with no joint-table entry for this node's jointId (an empty
        // table) also falls back to rest pose rather than animating.
        std::vector<JointChannels> noJoints;
        AnimationPose emptyTable{ &clip, &noJoints, 0.0f, false };
        ModelWalk fallback = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &emptyTable);
        Check(fallback.matrices[0].m[0][0] == 1.0f && fallback.matrices[0].m[0][1] == 0.0f,
              "a joint table with no entry for this node falls back to rest pose");
    }

    // A group node with a bendMatrix index (GEMD's carried MatrixID1) gets
    // its bend matrix rotated by half the sampled joint rotation (the
    // decomp's matrix1/HAS_MATRIX1 path) -- half of a 90-degree Z rotation
    // maps the local X axis 45 degrees toward world Y, not the full 90.
    {
        std::vector<ModelNode> nodes = { Group(0x00, 0, 0, 0, 0, kNoNode, kNoNode) };
        nodes[0].bendMatrix = 1;
        nodes[0].jointId = 0;

        uint8_t frame[6] = { 0x00, 0x00, 0x00, 0x00, 0x40, 0x00 }; // Z = pi/2, as above
        AnimationClip clip;
        clip.angleWidthBits = 16;
        clip.frameSizeBytes = sizeof(frame);
        clip.bitstream = frame;
        clip.frameCount = 1;
        std::vector<JointChannels> joints = { { 0, 0 } };
        AnimationPose pose{ &clip, &joints, 0.0f, false };

        ModelWalk animated = WalkModel(nodes, 2, IdentityMat4(), {}, {}, &pose);
        float diagonal = std::sqrt(2.0f) / 2.0f;
        Check(Near(animated.matrices[1].m[0][0], diagonal) && Near(animated.matrices[1].m[0][1], diagonal),
              "the bend matrix is rotated by half the ordinary matrix's angle");
        Check(Near(animated.matrices[0].m[0][0], 0.0f) && Near(animated.matrices[0].m[0][1], 1.0f),
              "the ordinary matrix still gets the full rotation");

        // Rest pose: half of a zero rotation is still zero, so the bend
        // matrix matches the ordinary (identity) one exactly.
        ModelWalk rest = WalkModel(nodes, 2, IdentityMat4());
        Check(rest.matrices[1].m[0][0] == 1.0f && rest.matrices[1].m[0][1] == 0.0f,
              "at rest pose the bend matrix is identity, same as the ordinary matrix");
    }

    // A pose with a secondaryClip blends its rotation with the primary
    // clip's via quaternion slerp by `blend`: a group node with a primary
    // rest-pose (0 degrees) and a secondary 90-degree Z rotation, blended
    // halfway, lands at 45 degrees -- the same result QuaternionTest's
    // "slerp halfway between 0 and 90 degrees" confirms for the underlying
    // math, wired through WalkModel.
    {
        std::vector<ModelNode> nodes = { Group(0x00, 0, 0, 0, 0, kNoNode, kNoNode) };
        nodes[0].jointId = 0;

        uint8_t restFrame[6] = { 0 };
        AnimationClip primaryClip;
        primaryClip.angleWidthBits = 16;
        primaryClip.frameSizeBytes = sizeof(restFrame);
        primaryClip.bitstream = restFrame;
        primaryClip.frameCount = 1;

        uint8_t quarterTurnFrame[6] = { 0x00, 0x00, 0x00, 0x00, 0x40, 0x00 }; // Z = pi/2
        AnimationClip secondaryClip;
        secondaryClip.angleWidthBits = 16;
        secondaryClip.frameSizeBytes = sizeof(quarterTurnFrame);
        secondaryClip.bitstream = quarterTurnFrame;
        secondaryClip.frameCount = 1;

        std::vector<JointChannels> joints = { { 0, 0 } };
        AnimationPose pose;
        pose.clip = &primaryClip;
        pose.joints = &joints;
        pose.secondaryClip = &secondaryClip;
        pose.blend = 0.5f;

        ModelWalk blended = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        float diagonal = std::sqrt(2.0f) / 2.0f;
        Check(Near(blended.matrices[0].m[0][0], diagonal) && Near(blended.matrices[0].m[0][1], diagonal),
              "blending halfway toward a 90-degree secondary rotation yields 45 degrees");

        // blend=0 (or no secondaryClip at all) is exactly the primary
        // rotation -- rest pose here, so the identity matrix.
        pose.blend = 0.0f;
        ModelWalk atZero = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(atZero.matrices[0].m[0][0], 1.0f) && Near(atZero.matrices[0].m[0][1], 0.0f),
              "blend=0 yields exactly the primary rotation");

        pose.secondaryClip = nullptr;
        ModelWalk noSecondary = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(noSecondary.matrices[0].m[0][0], 1.0f) && Near(noSecondary.matrices[0].m[0][1], 0.0f),
              "no secondaryClip falls back to the plain primary-only path");
    }

    if (gFailures == 0) {
        std::printf("ModelWalkTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
