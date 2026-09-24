#include "model/ModelInstance.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Frigate;
using Frigate::Model::ModelInstance;

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

ModelNode Node(uint32_t offset, uint8_t type, int16_t matrix, int32_t parent, int32_t child, int32_t next) {
    ModelNode node;
    node.offset = offset;
    node.type = type;
    node.matrix = matrix;
    node.parent = parent;
    node.child = child;
    node.next = next;
    return node;
}

ModelNode Draws(uint32_t offset, int32_t parent) {
    ModelNode node = Node(offset, 0x04, kNoMatrix, parent, kNoNode, kNoNode);
    node.primaryList = 0x100 + offset;
    return node;
}

std::shared_ptr<ModelTree> Tree(std::vector<ModelNode> nodes, uint32_t matrices, std::vector<int32_t> switches = {}) {
    auto tree = std::make_shared<ModelTree>();
    tree->Nodes = std::move(nodes);
    tree->MatrixCount = matrices;
    tree->Switches = std::move(switches);
    return tree;
}

// group(0, matrix 0) -> switch(1) -> draws(2); the switch is slot 0.
std::shared_ptr<ModelTree> SwitchedTree() {
    return Tree(
        { Node(0x00, 0x02, 0, kNoNode, 1, kNoNode), Node(0x10, 0x12, kNoMatrix, 0, 2, kNoNode), Draws(0x20, 1) }, 1,
        { 1 });
}

Mat4 Identity() {
    return IdentityMat4();
}

void TestARootHeaderMakesRootMotionAvailable() {
    ModelNode header = Node(0x00, 0x01, 0, kNoNode, kNoNode, kNoNode);
    header.jointId = 7;
    auto skeleton = std::make_shared<ModelSkeleton>();
    skeleton->Joints = { { 0, 1, 2 }, { 0, 3, 4 } };

    auto instance = ModelInstance::FromParts("chr", Tree({ header }, 1), skeleton);
    Check(instance->playback.hasRoot && instance->playback.rootJoint == 7, "a header root supplies the root joint");
    Check(instance->playback.joints != nullptr && instance->playback.joints->size() == 2 &&
              (*instance->playback.joints)[1].mtxB == 4,
          "the skeleton's joints become the playback's joint table");

    auto prop = ModelInstance::FromParts("prop", Tree({ Node(0x00, 0x15, 0, kNoNode, kNoNode, kNoNode) }, 1), nullptr);
    Check(!prop->playback.hasRoot, "a model without a header root has no root motion");
}

void TestSwitchSlotsAndVisibility() {
    auto instance = ModelInstance::FromParts("gun", SwitchedTree(), nullptr);
    Check(instance->SwitchNode(0) != nullptr && instance->SwitchNode(0)->type == 0x12, "a slot names its switch node");
    Check(instance->SwitchNode(1) == nullptr, "a slot past the array is empty");

    instance->Pose(Identity());
    Check(instance->Walk().lists.size() == 1, "every switch starts on");

    instance->SetSwitchVisible(0, false);
    instance->Pose(Identity());
    Check(instance->Walk().lists.empty(), "hiding a switch hides what is under it");

    instance->SetSwitchVisible(0, true);
    instance->Pose(Identity());
    Check(instance->Walk().lists.size() == 1, "showing it again brings it back");

    instance->SetSwitchVisible(5, false); // an empty slot: no effect, no crash
}

void TestGunfireVisibilityFollowsItsSwitchSlot() {
    // group -> gunfire (slot 0) and a switch (slot 1).
    auto instance = ModelInstance::FromParts(
        "gun",
        Tree({ Node(0x00, 0x02, 0, kNoNode, 1, kNoNode), Node(0x10, kGunfireNodeType, kNoMatrix, 0, kNoNode, 2),
               Node(0x20, 0x12, kNoMatrix, 0, kNoNode, kNoNode) },
             1, { 1, 2 }),
        nullptr);
    Check(!instance->GunfireVisible(0x10), "every flash starts hidden");

    instance->SetGunfireVisible(0, true);
    Check(instance->GunfireVisible(0x10), "showing a slot's flash");

    instance->SetGunfireVisible(0, false);
    Check(!instance->GunfireVisible(0x10), "hiding it again");

    instance->SetGunfireVisible(1, true); // a switch, not a flash
    instance->SetGunfireVisible(5, true); // an empty slot
    Check(!instance->GunfireVisible(0x20), "only a gunfire node can be shown as one");
}

void TestHeadAttachesAtSlotFourAndHidesItsFirstSwitch() {
    // body: group -> head placeholder (slot 4).
    auto body = ModelInstance::FromParts("chr",
                                         Tree({ Node(0x00, 0x02, 0, kNoNode, 1, kNoNode),
                                                Node(0x10, kHeadPlaceholderNodeType, kNoMatrix, 0, kNoNode, kNoNode) },
                                              1, { kNoNode, kNoNode, kNoNode, kNoNode, 1 }),
                                         nullptr);
    // head: switch (its slot 0) -> draws.
    auto head = Tree({ Node(0x00, 0x12, kNoMatrix, kNoNode, 1, kNoNode), Draws(0x10, 0) }, 0, { 0 });

    Check(body->AttachHeadTree("head", head, /*sunglasses=*/true), "a head attaches at the placeholder in slot 4");
    body->Pose(Identity());
    Check(body->Walk().attachments.size() == 1 && body->Walk().attachments[0].lists.size() == 1,
          "with sunglasses the head's first switch stays on");

    Check(body->AttachHeadTree("head", head, /*sunglasses=*/false), "attaching again replaces the head");
    body->Pose(Identity());
    Check(body->Walk().attachments[0].lists.empty(), "without sunglasses the head's first switch is hidden");

    auto headless = ModelInstance::FromParts("chr", SwitchedTree(), nullptr);
    Check(!headless->AttachHeadTree("head", head, true), "a model with no placeholder in slot 4 takes no head");
}

void TestAHeldModelIsBasedOnItsHoldersNode() {
    // holder: group (matrix 0) -> group (matrix 1, origin x = 5); slot 3 is the child.
    ModelNode child = Node(0x10, 0x02, 1, 0, kNoNode, kNoNode);
    child.origin[0] = 5.0f;
    auto holder = ModelInstance::FromParts(
        "chr", Tree({ Node(0x00, 0x02, 0, kNoNode, 1, kNoNode), child }, 2, { kNoNode, kNoNode, kNoNode, 1 }), nullptr);
    auto gun = ModelInstance::FromParts("gun", Tree({ Node(0x00, 0x15, 0, kNoNode, kNoNode, kNoNode) }, 1), nullptr);

    Check(!gun->AttachTo(holder, 2), "a holder's empty slot can't hold anything");
    Check(gun->AttachTo(holder, 3), "a holder's node can");

    holder->Pose(Identity());
    gun->Pose(Identity());
    Check(Near(gun->Walk().matrices[0].m[3][0], 5.0f), "the held model's matrices start from the holder's node");

    // Held models follow their holder as it moves.
    Mat4 moved = Identity();
    moved.m[3][0] = 100.0f;
    holder->Pose(moved);
    gun->Pose(moved);
    Check(Near(gun->Walk().matrices[0].m[3][0], 105.0f), "and follow it");
}

void TestLodScaleFollowsThePlaybackScale() {
    auto instance = ModelInstance::FromParts("chr", SwitchedTree(), nullptr);
    instance->playback.scale = 0.25f;
    instance->Pose(Identity());
    Check(Near(instance->lod.modelScale, 0.25f), "the LOD test uses the model's scale");
}

} // namespace

int main() {
    TestARootHeaderMakesRootMotionAvailable();
    TestSwitchSlotsAndVisibility();
    TestGunfireVisibilityFollowsItsSwitchSlot();
    TestHeadAttachesAtSlotFourAndHidesItsFirstSwitch();
    TestAHeldModelIsBasedOnItsHoldersNode();
    TestLodScaleFollowsThePlaybackScale();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All model instance tests passed\n");
    return 0;
}
