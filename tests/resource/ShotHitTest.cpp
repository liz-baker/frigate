#include <cmath>
#include <cstdio>
#include <memory>

#include "gfx/GbiF3dex.h"
#include "fast/indy.h"
#include "level/HitParts.h"
#include "level/LightFixtures.h"
#include "level/ModelHit.h"
#include "level/TextureHits.h"

using namespace Frigate;
using namespace Frigate::Level;
using Frigate::Model::ModelInstance;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
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

ModelNode HitBox(uint32_t offset, int32_t parent, int part, float zmin, float zmax) {
    ModelNode node = Node(offset, 0x0A, kNoMatrix, parent, kNoNode, kNoNode);
    node.hasBounds = true;
    node.hasHit = true;
    node.hitPart = part;
    node.bounds[0] = -10.0f;
    node.bounds[1] = 10.0f;
    node.bounds[2] = -10.0f;
    node.bounds[3] = 10.0f;
    node.bounds[4] = zmin;
    node.bounds[5] = zmax;
    return node;
}

std::shared_ptr<ModelTree> Tree(std::vector<ModelNode> nodes, std::vector<int32_t> switches = {}) {
    auto tree = std::make_shared<ModelTree>();
    tree->Nodes = std::move(nodes);
    tree->MatrixCount = 1;
    tree->Switches = std::move(switches);
    return tree;
}

const Frigate::Level::Vec3 kOrigin = { 0.0f, 0.0f, 0.0f };
const Frigate::Level::Vec3 kForward = { 0.0f, 0.0f, -1.0f };

void TestTextureHits() {
    const TextureHits hits = TextureHits::Parse("WALL 1 2\nGLASS 4 4\n");
    Check(hits.Size() == 2, "each line of the table is an image");
    Check(hits.Find(0) != nullptr && hits.Find(0)->sound == 1 && hits.Find(0)->impact == 2,
          "an image is found by its number, with its sound and impact surface");
    Check(hits.Find(1) != nullptr && hits.Find(1)->impact == HitType::Glass, "the second image is number 1");
    Check(hits.Find(5) == nullptr, "a number past the table is not an image");
    Check(SurfaceImpactsFor(HitType::Glass).count == 3, "glass leaves one of three marks");
    Check(SurfaceImpactsFor(HitType::Water).count == 0, "water leaves none");
}

void TestHitCandidate() {
    auto instance = ModelInstance::FromParts(
        "box", Tree({ Node(0, 0x02, 0, kNoNode, 1, kNoNode), HitBox(0x10, 0, HitPart::Chest, -110.0f, -90.0f) }),
        nullptr);
    instance->Pose(IdentityMat4());

    int32_t node = kNoNode;
    Check(FindProjectileHitCandidate(*instance, kOrigin, kForward, &node) == HitPart::Chest && node == 1,
          "a ray at a hit box finds its part and node");
    Check(FindProjectileHitCandidate(*instance, kOrigin, kForward, &node) == 0, "the walk ends after the last box");

    node = kNoNode;
    Check(FindProjectileHitCandidate(*instance, kOrigin, { 0.0f, 0.0f, 1.0f }, &node) == 0,
          "a ray away from the box misses it");
}

void TestHiddenSwitchHidesItsHitBox() {
    auto instance = ModelInstance::FromParts(
        "window",
        Tree({ Node(0, 0x02, 0, kNoNode, 1, kNoNode), Node(0x10, 0x12, kNoMatrix, 0, 2, kNoNode),
               HitBox(0x20, 1, HitPart::General, -110.0f, -90.0f) },
             { 1 }),
        nullptr);
    instance->Pose(IdentityMat4());
    int32_t node = kNoNode;
    Check(FindProjectileHitCandidate(*instance, kOrigin, kForward, &node) == HitPart::General,
          "a hit box under a shown switch is found");

    instance->SetSwitchVisible(0, false);
    instance->Pose(IdentityMat4());
    node = kNoNode;
    Check(FindProjectileHitCandidate(*instance, kOrigin, kForward, &node) == 0,
          "a hit box under a hidden switch is not");
}

// Hands out the triangle the test makes for the node at offset 0x10.
class FakeGeometry final : public ModelGeometrySource {
  public:
    FakeGeometry() {
        vertices_[0] = Vertex(-10, -10, -100);
        vertices_[1] = Vertex(10, -10, -100);
        vertices_[2] = Vertex(0, 10, -100);

        Gfx load;
        load.words.w0 = (static_cast<uint32_t>(static_cast<uint8_t>(G_VTX_OTR_HASH)) << 24) | (3u << 12) | (3u << 1);
        load.words.w1 = 0;
        list_[0] = load;
        list_[1] = Gfx{};
        list_[2] = F3dex::Triangle(0, 1, 2);
        list_[3] = F3dex::Command(Fast::F3DEX_G_ENDDL, 0, 0);

        geometry_ = std::make_shared<NodeGeometry>();
        geometry_->primary = list_;
        geometry_->primaryCount = 4;
        geometry_->vertices = vertices_;
        geometry_->vertexCount = 3;
    }

    std::shared_ptr<const NodeGeometry> LoadNode(const std::string&, const ModelNode& node) override {
        return node.offset == 0x10 ? geometry_ : nullptr;
    }

  private:
    static Vtx Vertex(int16_t x, int16_t y, int16_t z) {
        Vtx vertex{};
        vertex.v.ob[0] = x;
        vertex.v.ob[1] = y;
        vertex.v.ob[2] = z;
        return vertex;
    }

    Vtx vertices_[3];
    Gfx list_[4];
    std::shared_ptr<NodeGeometry> geometry_;
};

void TestTriangleHit() {
    ModelNode lists = Node(0x10, 0x04, kNoMatrix, 0, kNoNode, kNoNode);
    lists.primaryList = 0x100;
    auto instance = ModelInstance::FromParts("tri", Tree({ Node(0, 0x02, 0, kNoNode, 1, kNoNode), lists }), nullptr);
    instance->Pose(IdentityMat4());

    FakeGeometry geometry;
    HitThing hit;
    int matrixIndex = -1;
    int32_t hitNode = kNoNode;
    Check(FindTriangleHit(*instance, 0, kOrigin, kForward, geometry, nullptr, &hit, &matrixIndex, &hitNode),
          "a ray through a triangle of the node's list hits it");
    Check(std::fabs(hit.hitpos[2] + 100.0f) < 0.01f && hitNode == 1 && matrixIndex == 0,
          "the hit is on the triangle's plane, in the node and matrix that hold it");

    Check(!FindTriangleHit(*instance, 0, { 50.0f, 0.0f, 0.0f }, kForward, geometry, nullptr, &hit, &matrixIndex,
                           &hitNode),
          "a ray beside the triangle misses");
}

void TestSizes() {
    auto tree = Tree({ Node(0, 0x02, 0, kNoNode, kNoNode, kNoNode) });
    tree->BoundingRadius = 40.0f;
    auto instance = ModelInstance::FromParts("size", tree, nullptr);
    instance->playback.scale = 0.5f;
    Check(InstanceSize(*instance) == 20.0f, "a model's size is its bounding radius times its scale");
}

void TestLightTexture() {
    Check(!LightFixtures::IsLightTexture(0), "no image is the light that has no name");
}

} // namespace

int main() {
    TestTextureHits();
    TestHitCandidate();
    TestHiddenSwitchHidesItsHitBox();
    TestTriangleHit();
    TestSizes();
    TestLightTexture();
    if (gFailures == 0) {
        std::printf("all shot hit tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
