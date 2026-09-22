#pragma once

#include <vector>

#include "model/ModelNode.h"
#include "ship/resource/Resource.h"

// A model's node tree ("GEMD"), from the packer's `<model>/tree` resource.
// Payload: docs/formats/goldeneye-model-tree-resource.md.
namespace Frigate {

class ModelTree final : public Ship::Resource<ModelNode> {
  public:
    using Resource::Resource;

    ModelTree();

    ModelNode* GetPointer() override;
    size_t GetPointerSize() override;

    // Preorder, so the root is first.
    std::vector<ModelNode> Nodes;
    uint32_t MatrixCount = 0;
};

} // namespace Frigate
