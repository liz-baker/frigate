#pragma once

#include "extract/DisplayListEmit.h"
#include "extract/Error.h"
#include "extract/ModelContainer.h"
#include "extract/ModelTables.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace Frigate::Extract {

// A model file's node tree and the resources made of it. On-disk node layout: docs/formats/goldeneye-model-nodes.md;
// the GEMD payload: docs/formats/goldeneye-model-tree-resource.md; the GESK payload:
// docs/formats/goldeneye-model-skeleton-resource.md.

constexpr uint32_t kNodeSize = 0x18;
constexpr uint32_t kTextureEntrySize = 12;

constexpr uint32_t kNodeDl = 0x04;
constexpr uint32_t kNodeLod = 0x08;
constexpr uint32_t kNodeBsp = 0x09;
constexpr uint32_t kNodeGunfire = 0x0C;
constexpr uint32_t kNodeShadow = 0x0D;
constexpr uint32_t kNodeSwitch = 0x12;
constexpr uint32_t kNodeDlPrimary = 0x16;
constexpr uint32_t kNodeDlCollision = 0x18;

constexpr const char* kModelTreeResourceType = "GEMD";
constexpr uint32_t kModelTreeResourceVersion = 8;
constexpr const char* kSkeletonResourceType = "GESK";
constexpr uint32_t kSkeletonResourceVersion = 0;

// A display-list node's lists and vertex array. `verticesAddress` is the raw pointer, which
// ModelImage::DisplayListResolver binds as segment 4. Offsets are into the image; `secondary` is absent for a node
// with no second list. `vertexBytes` is the array size the node's count field gives.
struct DisplayListRecord {
    std::optional<uint32_t> primary;
    std::optional<uint32_t> secondary;
    std::optional<uint32_t> vertices;
    uint32_t verticesAddress = 0;
    uint32_t vertexBytes = 0;
};

struct ModelNode {
    uint32_t offset = 0;
    uint32_t type = 0;
    std::optional<uint32_t> data;
    std::optional<uint32_t> parent;
    std::optional<uint32_t> next;
    std::optional<uint32_t> prev;
    std::optional<uint32_t> child;
    std::optional<DisplayListRecord> displayList;
};

struct ModelTexture {
    uint32_t textureId = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t mipMapTiles = 0;
    uint8_t type = 0;
    uint8_t renderDepth = 0;
    uint8_t sFlags = 0;
    uint8_t tFlags = 0;
};

struct ModelTree {
    uint32_t root = 0;
    std::map<uint32_t, ModelNode> nodes; // by offset
    std::vector<ModelTexture> textures;
    std::vector<std::optional<uint32_t>> switches;
};

// A gunfire or shadow node's drawing data: the size of the quad it draws (a shadow's z is 0, here as the zero word)
// and its texture table entry, absent for an untextured quad.
struct EffectRecord {
    uint32_t size[3] = {};
    std::optional<ModelTexture> texture;
};

// Reads the switch array, texture table and every node reachable from the root. Fails for a link that leaves the
// image or a node reached by two different links.
Error ParseModelTree(const ModelImage& image, const ModelHeaderEntry& header, ModelTree& tree);

// The drawing data of a gunfire or shadow node; nullopt for any other node type.
Error ReadEffectRecord(const ModelImage& image, const ModelTree& tree, const ModelNode& node,
                       std::optional<EffectRecord>& record);

// One display-list node's vertex array: where it starts in the image and how long it is.
struct ModelVertexBlock {
    uint32_t node = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
};

// One block per display-list node that has an array, in node-offset order. Nodes that share an array each get their
// own block, since their lists address it relative to the node.
Error FindModelVertexBlocks(const ModelImage& image, const ModelTree& tree, std::vector<ModelVertexBlock>& blocks);

// The GEMD payload. `textures` resolves the image of a textured gunfire or shadow node to its N64 format.
Error EncodeModelTreePayload(const ModelImage& image, const ModelTree& tree, const TextureLookup& textures,
                             float radius, std::vector<uint8_t>& payload);

// The GESK payload of a skeleton's joints.
std::vector<uint8_t> EncodeSkeletonPayload(std::span<const SkeletonJoint> joints);

// The G_SETTEX word a gunfire or shadow node's image is selected with: the game's texSelect() loads it as one tile
// with a half-texel sample offset, through its own wrap flags.
uint32_t EffectImageRequest(const ModelTexture& texture);

} // namespace Frigate::Extract
