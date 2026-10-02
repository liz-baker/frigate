#pragma once

#include <vector>

#include "model/ModelNode.h"
#include "ship/resource/Resource.h"

// A model's node tree ("GEMD"), from the packer's `<model>/tree` resource.
// Payload: docs/formats/goldeneye-model-tree-resource.md.
namespace Frigate {

// A collision display-list node's vertex table ("Collision table" in the
// resource doc).
struct ModelCollision {
    struct Vertex {
        int16_t position[3];
        // The first vertex at the point.
        int16_t index;
        // The node holding the related vertex (an index into Nodes), or kNoNode.
        int32_t relatedNode;
        int16_t relatedIndex;
    };
    uint16_t vertexCount = 0;
    std::vector<Vertex> collisionVertices;
    // The next vertex at the same point as each vertex, or -1.
    std::vector<int16_t> pointUsage;
};

class ModelTree final : public Ship::Resource<ModelNode> {
  public:
    using Resource::Resource;

    ModelTree();

    ModelNode* GetPointer() override;
    size_t GetPointerSize() override;

    // Preorder, so the root is first.
    std::vector<ModelNode> Nodes;
    uint32_t MatrixCount = 0;
    // The model's switch array: node indices (into Nodes) or kNoNode. Game
    // code addresses these by position (e.g. the node a held gun attaches to).
    std::vector<int32_t> Switches;
    std::vector<ModelCollision> Collisions;
    // getinstsize's radius before the model's scale.
    float BoundingRadius = 0.0f;
};

} // namespace Frigate
