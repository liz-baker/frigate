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

    // A head attachment's own switches are hidden by the attachment's list
    // (e.g. a head's sunglasses), not by the body's.
    {
        std::vector<ModelNode> body = { HeadPlaceholder(0x20, kNoNode) };
        std::vector<ModelNode> head = { Switch(0x00, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };

        std::vector<HeadAttachment> shown = { { 0x20, &head, 1 } };
        Check(WalkModel(body, 1, base, shown).attachments[0].lists.size() == 1,
              "a head's switch is on unless the attachment hides it");

        std::vector<HeadAttachment> hidden = { { 0x20, &head, 1, { 0x00 } } };
        Check(WalkModel(body, 1, base, hidden).attachments[0].lists.empty(),
              "a head attachment's hidden switch draws nothing");
        Check(WalkModel(body, 1, base, shown, { 0x00 }).attachments[0].lists.size() == 1,
              "the body's hidden list doesn't reach into the head");
    }

    // A switch node's child is visited unless its offset is in hiddenSwitches:
    // every switch starts on.
    {
        std::vector<ModelNode> withSwitch = { Switch(0x00, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };

        ModelWalk on = WalkModel(withSwitch, 1, base);
        Check(on.lists.size() == 1 && on.lists[0].list == 0x100, "an unlisted switch is on by default");

        ModelWalk off = WalkModel(withSwitch, 1, base, {}, { 0x00 });
        Check(off.lists.empty(), "a hidden switch draws nothing");
    }

    // The walk records the nodes it visits with their depth: a gated-off
    // switch's child is absent, and a head's nodes follow their placeholder,
    // tagged with the attachment they belong to.
    {
        // group(0) -> [switch(1, off) -> draws(2)], placeholder(3) ; sibling group(4)
        std::vector<ModelNode> body = { Group(0x00, 0, 0, 0, 0, 1, 4), Switch(0x18, 2, 3),
                                        Draws(0x30, 0x100, kNoList, kNoNode), HeadPlaceholder(0x48, kNoNode),
                                        Group(0x60, 1, 0, 0, 0, kNoNode, kNoNode) };
        body[1].parent = 0;
        body[3].parent = 0;
        body[1].next = 3;
        std::vector<ModelNode> head = { Draws(0x00, 0x200, kNoList, kNoNode) };
        std::vector<HeadAttachment> attachments = { { 0x48, &head, 1 } };

        ModelWalk walk = WalkModel(body, 2, base, attachments, { 0x18 });
        std::vector<std::vector<int32_t>> got;
        for (const VisitedNode& v : walk.visited) {
            got.push_back({ static_cast<int32_t>(v.node), v.attachment, static_cast<int32_t>(v.depth) });
        }
        std::vector<std::vector<int32_t>> expected = {
            { 0, -1, 0 }, { 1, -1, 1 }, { 3, -1, 1 }, { 0, 0, 2 }, { 4, -1, 0 }
        };
        Check(got == expected, "visited nodes come in traversal order with their depth and head attachment");
    }

    // The LOD test follows the decomp: distance is the depth times the scales,
    // compared against the minimum and maximum times the model's scale, or 0
    // when distance is disabled.
    {
        Mat4 far = IdentityMat4();
        far.m[3][2] = -50.0f;
        std::vector<ModelNode> lod = { Lod(0x00, 10.0f, 100.0f, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };
        std::vector<ModelNode> nearest = { Lod(0x00, 0.0f, 100.0f, 1, kNoNode), Draws(0x18, 0x100, kNoList, kNoNode) };

        LodSettings scaled;
        scaled.modelScale = 0.1f; // range becomes (1, 10]: 50 is beyond it
        Check(WalkModel(lod, 1, far, {}, {}, nullptr, nullptr, &scaled).lists.empty(),
              "the model's scale shrinks the LOD range");

        LodSettings zScaled;
        zScaled.zScale = 0.1f; // distance 5, below the minimum
        Check(WalkModel(lod, 1, far, {}, {}, nullptr, nullptr, &zScaled).lists.empty(),
              "the depth scale shrinks the distance");

        LodSettings disabled;
        disabled.distanceDisabled = true;
        Check(WalkModel(lod, 1, far, {}, {}, nullptr, nullptr, &disabled).lists.empty(),
              "with distance disabled a LOD child with a minimum distance is hidden");
        Check(WalkModel(nearest, 1, far, {}, {}, nullptr, nullptr, &disabled).lists.size() == 1,
              "with distance disabled the highest-detail child (minimum 0) shows");

        Mat4 sideways = IdentityMat4();
        sideways.m[3][0] = 500.0f; // far off to the side, but at depth 0
        Check(WalkModel(nearest, 1, sideways).lists.size() == 1,
              "distance is depth, not the length of the translation");
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

        // In the file the second group's head follows the first group in the
        // sibling chain; each group still draws once, ending where the other
        // begins.
        for (const float pointX : { -1.0f, 1.0f }) {
            std::vector<ModelNode> linked = { Bsp(0x00, pointX, 1.0f, 1, 3, kNoNode), Draws(0x18, 0x100, kNoList, 2),
                                              Draws(0x30, 0x200, kNoList, 3), Draws(0x48, 0x300, kNoList, kNoNode) };
            ModelWalk chain = WalkModel(linked, 1, IdentityMat4());
            const std::vector<uint32_t> expected = pointX < 0.0f ? std::vector<uint32_t>{ 0x100, 0x200, 0x300 }
                                                                 : std::vector<uint32_t>{ 0x300, 0x100, 0x200 };
            bool same = chain.lists.size() == expected.size();
            for (size_t i = 0; same && i < expected.size(); i++) {
                same = chain.lists[i].list == expected[i];
            }
            Check(same, "linked BSP groups each draw exactly once");
        }

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
        AnimationPose pose;
        pose.clip = &clip;
        pose.joints = &joints;

        ModelWalk animated = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(animated.matrices[0].m[0][0], 0.0f) && Near(animated.matrices[0].m[0][1], 1.0f),
              "a 90-degree Z rotation maps the local X axis onto world Y");

        ModelWalk rest = WalkModel(nodes, 1, IdentityMat4());
        Check(rest.matrices[0].m[0][0] == 1.0f && rest.matrices[0].m[0][1] == 0.0f,
              "the same node with no pose stays at rest pose");

        // A pose with no joint-table entry for this node's jointId (an empty
        // table) also falls back to rest pose rather than animating.
        std::vector<JointChannels> noJoints;
        AnimationPose emptyTable;
        emptyTable.clip = &clip;
        emptyTable.joints = &noJoints;
        ModelWalk fallback = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &emptyTable);
        Check(fallback.matrices[0].m[0][0] == 1.0f && fallback.matrices[0].m[0][1] == 0.0f,
              "a joint table with no entry for this node falls back to rest pose");
    }

    // A pose blends its two frames by the frame fraction: a joint whose Z is 0
    // on frame 0 and pi/2 on frame 1 lands at 45 degrees halfway between them.
    {
        std::vector<ModelNode> nodes = { Group(0x00, 0, 0, 0, 0, kNoNode, kNoNode) };
        nodes[0].jointId = 0;

        uint8_t frames[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0x00 };
        AnimationClip clip;
        clip.angleWidthBits = 16;
        clip.frameSizeBytes = 6;
        clip.bitstream = frames;
        clip.frameCount = 2;
        std::vector<JointChannels> joints = { { 0, 0 } };

        AnimationPose pose;
        pose.clip = &clip;
        pose.joints = &joints;
        pose.frameA = 0;
        pose.frameB = 1;
        pose.frameFrac = 0.5f;

        float diagonal = std::sqrt(2.0f) / 2.0f;
        ModelWalk halfway = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(halfway.matrices[0].m[0][0], diagonal) && Near(halfway.matrices[0].m[0][1], diagonal),
              "halfway between two frames is halfway between their rotations");

        pose.frameA = 1;
        pose.frameFrac = 0.0f;
        ModelWalk second = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(second.matrices[0].m[0][0], 0.0f) && Near(second.matrices[0].m[0][1], 1.0f),
              "a zero fraction is frameA alone");

        // The secondary clip has its own mirror flag: mirrored, its 90-degree Z
        // becomes 270, and a full-weight blend lands there.
        pose.frameA = 0;
        pose.secondaryClip = &clip;
        pose.secondaryMirror = true;
        pose.secondaryFrameA = 1;
        pose.blend = 1.0f;
        ModelWalk mirrored = WalkModel(nodes, 1, IdentityMat4(), {}, {}, &pose);
        Check(Near(mirrored.matrices[0].m[0][0], 0.0f) && Near(mirrored.matrices[0].m[0][1], -1.0f),
              "the secondary clip is mirrored by its own flag");
    }

    // A header node with a RootPlacement is placed by it (a Y rotation, a
    // translation, and a scale on the 3x3 part) instead of owning the
    // inherited matrix unchanged; its children compose with the result.
    {
        ModelNode header;
        header.offset = 0x00;
        header.type = 0x01;
        header.matrix = 0;
        header.child = 1;
        std::vector<ModelNode> nodes = { header, Group(0x10, 1, 1, 0, 0, kNoNode, kNoNode) };

        ModelWalk unplaced = WalkModel(nodes, 2, IdentityMat4());
        Check(unplaced.matrices[0].m[3][0] == 0.0f && unplaced.matrices[0].m[0][0] == 1.0f,
              "a header node with no placement owns the inherited matrix unchanged");

        RootPlacement placement;
        placement.position[0] = 10.0f;
        placement.position[1] = 20.0f;
        placement.position[2] = 30.0f;
        placement.heading = 3.14159265f / 2.0f;
        placement.scale = 2.0f;
        ModelWalk placed = WalkModel(nodes, 2, IdentityMat4(), {}, {}, nullptr, &placement);
        const Mat4& root = placed.matrices[0];
        Check(Near(root.m[0][0], 0.0f) && Near(root.m[0][2], -2.0f) && Near(root.m[1][1], 2.0f) &&
                  Near(root.m[2][0], 2.0f) && Near(root.m[2][2], 0.0f),
              "the placement rotates about Y by the heading, scaled");
        Check(Near(root.m[3][0], 10.0f) && Near(root.m[3][1], 20.0f) && Near(root.m[3][2], 30.0f),
              "the placement's translation is not scaled");
        // The child's origin (1, 0, 0) goes through the header's rotation and scale.
        Check(Near(placed.matrices[1].m[3][0], 10.0f) && Near(placed.matrices[1].m[3][2], 28.0f),
              "a child of the placed header composes with its matrix");
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
        AnimationPose pose;
        pose.clip = &clip;
        pose.joints = &joints;

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

    {
        // A model whose matrices the caller built: root group (matrix 0), a
        // second group under it (matrix 1) and a switch with a list under it. The
        // nodes' own origins play no part; every list is under a matrix given.
        std::vector<ModelNode> nodes = { Group(0x00, 0, 5, 5, 5, 1, kNoNode), Group(0x10, 1, 9, 9, 9, 2, 3),
                                         Draws(0x20, 0x100, kNoList, kNoNode), Switch(0x30, 4, kNoNode),
                                         Draws(0x40, 0x200, kNoList, kNoNode) };
        nodes[1].parent = 0;
        nodes[2].parent = 1;
        nodes[3].parent = 0;
        nodes[4].parent = 3;
        // The second group is the root's child and the switch its sibling.
        nodes[1].next = 3;
        nodes[2].next = kNoNode;

        std::vector<Mat4> matrices = { IdentityMat4(), IdentityMat4() };
        matrices[0].m[3][0] = 1.0f;
        matrices[1].m[3][0] = 2.0f;

        ModelWalk walk = WalkModelWithMatrices(nodes, matrices);
        Check(walk.matrices.size() == 2 && Near(walk.matrices[0].m[3][0], 1.0f) &&
                  Near(walk.matrices[1].m[3][0], 2.0f) && Near(walk.matrices[1].m[3][1], 0.0f),
              "the caller's matrices are the walk's, with no origin added");
        Check(walk.lists.size() == 2 && walk.lists[0].list == 0x100 && walk.lists[1].list == 0x200,
              "every list of a shown switch is walked");

        walk = WalkModelWithMatrices(nodes, matrices, { 0x30 });
        Check(walk.lists.size() == 1 && walk.lists[0].list == 0x100, "a hidden switch hides what is under it");

        // A level-of-detail node reads the depth of the matrix above it.
        ModelNode lod = Lod(0x50, 0.0f, 10.0f, 1, kNoNode);
        std::vector<ModelNode> lodNodes = { Group(0x00, 0, 0, 0, 0, 1, kNoNode), lod,
                                            Draws(0x60, 0x300, kNoList, kNoNode) };
        lodNodes[1].parent = 0;
        lodNodes[1].child = 2;
        lodNodes[2].parent = 1;
        std::vector<Mat4> near = { IdentityMat4() };
        near[0].m[3][2] = -5.0f;
        std::vector<Mat4> far = { IdentityMat4() };
        far[0].m[3][2] = -50.0f;
        Check(WalkModelWithMatrices(lodNodes, near).lists.size() == 1, "a node within range shows");
        Check(WalkModelWithMatrices(lodNodes, far).lists.empty(), "a node past the range hides");
    }

    if (gFailures == 0) {
        std::printf("ModelWalkTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
