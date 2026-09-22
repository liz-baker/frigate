#pragma once

#include <cstdint>

// One record of a packed model node tree. Layout and field meaning:
// docs/formats/goldeneye-model-tree-resource.md.
namespace Frigate {

constexpr int32_t kNoNode = -1;
constexpr int16_t kNoMatrix = -1;
constexpr uint32_t kNoList = 0xFFFFFFFFu;

struct ModelNode {
    uint32_t offset = 0; // the node's position in the model file; keys its list resources
    uint8_t type = 0;
    int16_t matrix = kNoMatrix;
    int16_t bendMatrix = kNoMatrix;
    int32_t child = kNoNode; // indices into the tree's node array
    int32_t next = kNoNode;
    int32_t parent = kNoNode;
    float origin[3] = { 0, 0, 0 };
    uint32_t primaryList = kNoList; // file offsets, the second half of a list resource's path
    uint32_t secondaryList = kNoList;
};

} // namespace Frigate
