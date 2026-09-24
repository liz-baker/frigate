#include "model/JointDrawList.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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

ModelNode Node(uint32_t offset, uint8_t type, int32_t parent, int32_t child, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = type;
    node.parent = parent;
    node.child = child;
    node.next = next;
    return node;
}

ModelNode Joint(uint32_t offset, uint8_t type, int16_t matrix, float z, int32_t parent, int32_t child, int32_t next) {
    ModelNode node = Node(offset, type, parent, child, next);
    node.matrix = matrix;
    node.origin[2] = z;
    return node;
}

ModelNode Draws(uint32_t offset, int32_t parent, int32_t next) {
    ModelNode node = Node(offset, 0x04, parent, kNoNode, next);
    node.primaryList = 0x100 + offset;
    return node;
}

// header -> g1 (z -100) -> g2 (z -50, so -150 in all); the header names g1 as
// its first group and g1 names g2 as its child group.
std::vector<ModelNode> Chain() {
    std::vector<ModelNode> nodes = {
        Joint(0x00, 0x01, 0, 0.0f, kNoNode, 1, kNoNode),
        Joint(0x10, 0x02, 1, -100.0f, 0, 2, kNoNode),
        Joint(0x20, 0x02, 2, -50.0f, 1, kNoNode, kNoNode),
    };
    nodes[0].sortOther = 1;
    nodes[1].sortOther = 2;
    return nodes;
}

void TestJointsSortFarthestFirst() {
    std::vector<ModelNode> nodes = Chain();
    ModelWalk walk = WalkModel(nodes, 3, IdentityMat4());
    std::vector<JointModel> models(1);
    models[0].nodes = &nodes;
    models[0].walk = &walk;

    std::vector<JointEntry> entries = BuildJointEntries(models);
    Check(entries.size() == 3 && entries[0].visit == 0 && entries[2].visit == 2,
          "one entry per joint node, in visit order");

    ComputeJointSortValues(entries, models);
    // The header sits halfway between itself (0) and g1 (-100); g1 halfway
    // between itself (-100) and g2 (-150); g2 has no group below it.
    Check(Near(entries[0].sortValue, 50.0f) && Near(entries[1].sortValue, 125.0f) && Near(entries[2].sortValue, 150.0f),
          "a joint's depth is the average with its child group's, or its own without one");

    SortJointEntries(entries);
    Check(entries[0].visit == 2 && entries[1].visit == 1 && entries[2].visit == 0, "joints draw farthest first");
}

void TestEqualDepthsKeepTheirOrder() {
    std::vector<ModelNode> nodes = {
        Joint(0x00, 0x15, 0, -10.0f, kNoNode, kNoNode, 1),
        Joint(0x10, 0x15, 1, -10.0f, kNoNode, kNoNode, 2),
        Joint(0x20, 0x15, 2, -10.0f, kNoNode, kNoNode, kNoNode),
    };
    ModelWalk walk = WalkModel(nodes, 3, IdentityMat4());
    std::vector<JointModel> models(1);
    models[0].nodes = &nodes;
    models[0].walk = &walk;

    std::vector<JointEntry> entries = BuildSortedJointEntries(models);
    Check(entries.size() == 3 && entries[0].visit == 0 && entries[1].visit == 1 && entries[2].visit == 2,
          "entries of equal depth keep their order");
}

void TestPointNodesUseTheirPointInTheMatrixSpace() {
    // A group at z -100 with a gunfire node (point (0, 0, -20)), a shadow (x 0,
    // z 0, height 30 above the ground) and an interlink whose two points are at
    // depth 5 and -5.
    std::vector<ModelNode> nodes = {
        Joint(0x00, 0x02, 0, -100.0f, kNoNode, 1, kNoNode),
        Node(0x10, 0x0c, 0, kNoNode, 2),
        Node(0x20, 0x0d, 0, kNoNode, 3),
        Node(0x30, 0x0f, 0, kNoNode, kNoNode),
    };
    nodes[1].sortPoint[2] = -20.0f;
    nodes[3].sortPoint[2] = 5.0f;
    nodes[3].sortPoint2[2] = -5.0f;
    ModelWalk walk = WalkModel(nodes, 1, IdentityMat4());
    std::vector<JointModel> models(1);
    models[0].nodes = &nodes;
    models[0].walk = &walk;
    models[0].shadowHeight = 30.0f;

    std::vector<JointEntry> entries = BuildJointEntries(models);
    ComputeJointSortValues(entries, models);
    Check(entries.size() == 4, "gunfire, shadow and interlink nodes start entries too");
    Check(Near(entries[1].sortValue, 120.0f), "a gunfire node is sorted at its offset in its matrix's space");
    // Identity rotation: the shadow's height is along Y, which doesn't reach depth.
    Check(Near(entries[2].sortValue, 100.0f), "a shadow node is sorted at its position");
    Check(Near(entries[3].sortValue, std::min(95.0f, 105.0f)),
          "an interlink node is sorted at the nearer of its two points");
}

void TestEntriesAcrossModelsSortTogether() {
    std::vector<ModelNode> chr = { Joint(0x00, 0x02, 0, -100.0f, kNoNode, kNoNode, kNoNode) };
    std::vector<ModelNode> gun = { Joint(0x00, 0x15, 0, -200.0f, kNoNode, kNoNode, kNoNode) };
    ModelWalk chrWalk = WalkModel(chr, 1, IdentityMat4());
    ModelWalk gunWalk = WalkModel(gun, 1, IdentityMat4());
    std::vector<JointModel> models(2);
    models[0].nodes = &chr;
    models[0].walk = &chrWalk;
    models[1].nodes = &gun;
    models[1].walk = &gunWalk;

    std::vector<JointEntry> entries = BuildJointEntries(models);
    Check(entries.size() == 2 && entries[0].model == 0 && entries[1].model == 1,
          "models' entries are listed in model order");

    entries = BuildSortedJointEntries(models);
    Check(entries[0].model == 1 && entries[1].model == 0, "entries of different models sort by depth together");
}

void TestEntryNodesSkipOtherJointsAndTheirSubtrees() {
    // g0 -> [draws1, g2 -> [draws3], draws4]
    std::vector<ModelNode> nodes = {
        Joint(0x00, 0x02, 0, 0.0f, kNoNode, 1, kNoNode),
        Draws(0x10, 0, 2),
        Joint(0x20, 0x02, 1, 0.0f, 0, 3, 4),
        Draws(0x30, 2, kNoNode),
        Draws(0x40, 0, kNoNode),
    };
    ModelWalk walk = WalkModel(nodes, 2, IdentityMat4());
    JointModel model;
    model.nodes = &nodes;
    model.walk = &walk;

    std::vector<size_t> outer = JointEntryNodes(model, 0);
    Check(outer.size() == 2 && walk.visited[outer[0]].node == 1 && walk.visited[outer[1]].node == 4,
          "an entry draws its own nodes but not another joint's or what is below it");

    std::vector<size_t> inner = JointEntryNodes(model, 2);
    Check(inner.size() == 1 && walk.visited[inner[0]].node == 3, "a nested joint's entry draws its own subtree");
}

void TestHeadNodesBelongToTheirPlaceholdersJoint() {
    // neck group -> placeholder; the head's one node draws with the neck.
    std::vector<ModelNode> body = {
        Joint(0x00, 0x02, 0, 0.0f, kNoNode, 1, kNoNode),
        Node(0x10, kHeadPlaceholderNodeType, 0, kNoNode, kNoNode),
    };
    std::vector<ModelNode> head = { Draws(0x00, kNoNode, kNoNode) };
    std::vector<HeadAttachment> attachments = { { 0x10, &head, 1 } };
    ModelWalk walk = WalkModel(body, 1, IdentityMat4(), attachments);
    JointModel model;
    model.nodes = &body;
    model.walk = &walk;
    model.attachmentNodes = { &head };

    std::vector<JointEntry> entries = BuildJointEntries({ model });
    Check(entries.size() == 1, "a head's node doesn't start an entry");

    std::vector<size_t> nodes = JointEntryNodes(model, 0);
    Check(nodes.size() == 2 && walk.visited[nodes[1]].attachment == 0 &&
              VisitedModelNode(model, walk.visited[nodes[1]]).type == 0x04,
          "the placeholder and the head's nodes are drawn with the joint they hang from");
}

} // namespace

int main() {
    TestJointsSortFarthestFirst();
    TestEqualDepthsKeepTheirOrder();
    TestPointNodesUseTheirPointInTheMatrixSpace();
    TestEntriesAcrossModelsSortTogether();
    TestEntryNodesSkipOtherJointsAndTheirSubtrees();
    TestHeadNodesBelongToTheirPlaceholdersJoint();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All joint draw list tests passed\n");
    return 0;
}
