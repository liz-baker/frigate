#pragma once

#include <cstdint>
#include <limits>

// One record of a packed model node tree. Layout and field meaning:
// docs/formats/goldeneye-model-tree-resource.md.
namespace Frigate {

constexpr int32_t kNoNode = -1;
constexpr int16_t kNoMatrix = -1;
constexpr uint32_t kNoList = 0xFFFFFFFFu;

// A body model's placeholder for a head file grafted onto it at runtime;
// which head (if any) is a per-character choice outside the model file.
constexpr uint8_t kHeadPlaceholderNodeType = 0x17;

// Gates its single child on/off; the on/off state is set by gameplay code
// specific to the model (e.g. a character's held-item logic), not carried
// in the model file itself.
constexpr uint8_t kSwitchNodeType = 0x12;

// Gates its single child (the LOD table entry's data record `Affects`
// pointer, already resolved into the node's own child link) on the
// current render distance falling in [lodMinDistance, lodMaxDistance).
constexpr uint8_t kLodNodeType = 0x08;

// Reorders its two child groups (the node's own child link, then the BSP
// table entry's second group) by a plane test against the node's own
// inherited matrix; both groups are always visited.
constexpr uint8_t kBspNodeType = 0x09;

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
    // Only meaningful for a kLodNodeType node; the LOD table entry's fields.
    float lodMinDistance = 0.0f;
    float lodMaxDistance = std::numeric_limits<float>::infinity();
    // Only meaningful for a kBspNodeType node; the BSP table entry's fields.
    float bspPoint[3] = { 0, 0, 0 };
    float bspVector[3] = { 0, 0, 0 };
    int32_t bspTransformMode = 0;
    int32_t bspSecondChild = kNoNode;
    // Only meaningful for a group node (type 0x02/0x03); indexes the
    // model's joint table (not yet carried by this resource -- #70).
    uint16_t jointId = 0;
};

} // namespace Frigate
