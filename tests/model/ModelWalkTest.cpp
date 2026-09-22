#include "model/ModelWalk.h"

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

    if (gFailures == 0) {
        std::printf("ModelWalkTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
