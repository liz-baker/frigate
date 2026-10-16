#include "extract/ModelTree.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace Frigate::Extract {

namespace {

// Raised by the readers below and turned into an Error at the public functions.
struct ModelFail {
    std::string what;
};

[[noreturn]] void Fail(const std::string& what) {
    throw ModelFail{ what };
}

std::string Hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%x", value);
    return text;
}

std::string NodeName(uint32_t offset) {
    return "node at 0x" + Hex(offset);
}

Error ToError(const ModelFail& fail) {
    return Error::Make(ErrorCode::Corrupt, "A model in the ROM does not have the expected layout: " + fail.what + ".");
}

constexpr uint32_t kNodeHeader = 0x01;
constexpr uint32_t kNodeGroup = 0x02;
constexpr uint32_t kNodeGroupAlt = 0x03;
constexpr uint32_t kNodeGroupSimple = 0x15;
constexpr uint32_t kNodeInterlink = 0x0F;
constexpr uint32_t kNodeBoundingBox = 0x0A;
constexpr uint32_t kNodeHitSphere = 0x11;
// Draw-order node types the game handles but no US ROM model contains.
constexpr uint32_t kUnpackedSortNodeTypes[] = { 0x03, 0x0B, 0x0E, 0x10 };

constexpr int32_t kNoIndex = -1;
constexpr uint32_t kNoList = 0xFFFFFFFF;
constexpr uint32_t kMaxRenderType = 4;
constexpr uint32_t kPointerSize = 4;
constexpr uint32_t kVertexBytes = 16;
// A primary-only node's count is in groups of four vertices: in all five such nodes across the US ROM's model
// files, the lists load exactly four times the count.
constexpr uint32_t kPrimaryOnlyVerticesPerCount = 4;

// Offsets into a gunfire node's data record and a shadow's.
constexpr uint32_t kGunfireSizeOffset = 0x0C;
constexpr uint32_t kGunfireImageOffset = 0x18;
constexpr uint32_t kShadowSizeOffset = 0x08;
constexpr uint32_t kShadowImageOffset = 0x10;

constexpr uint32_t kRequestTypeMipmapValue = 2;

// A byte reader over the image that raises ModelFail past its end.
class Bytes {
  public:
    explicit Bytes(const std::vector<uint8_t>& data) : mData(data) {
    }

    size_t size() const {
        return mData.size();
    }
    uint32_t U8(size_t at) const {
        Need(at, 1);
        return mData[at];
    }
    uint32_t U16(size_t at) const {
        Need(at, 2);
        return (uint32_t(mData[at]) << 8) | mData[at + 1];
    }
    int32_t S16(size_t at) const {
        return int16_t(U16(at));
    }
    uint32_t U32(size_t at) const {
        Need(at, 4);
        return (uint32_t(mData[at]) << 24) | (uint32_t(mData[at + 1]) << 16) | (uint32_t(mData[at + 2]) << 8) |
               uint32_t(mData[at + 3]);
    }
    int32_t S32(size_t at) const {
        return int32_t(U32(at));
    }

  private:
    void Need(size_t at, size_t count) const {
        if (at > mData.size() || count > mData.size() - at) {
            Fail("a record runs off the model file");
        }
    }
    const std::vector<uint8_t>& mData;
};

// Appends big-endian fields. Floats are carried as their words, so the bytes are the file's own.
class Out {
  public:
    explicit Out(std::vector<uint8_t>& bytes) : mBytes(bytes) {
    }
    void U8(uint32_t v) {
        mBytes.push_back(uint8_t(v));
    }
    void U16(uint32_t v) {
        U8(v >> 8);
        U8(v);
    }
    void U32(uint32_t v) {
        U16(v >> 16);
        U16(v);
    }
    void S32(int32_t v) {
        U32(uint32_t(v));
    }
    void Pad(size_t count) {
        mBytes.insert(mBytes.end(), count, 0);
    }

  private:
    std::vector<uint8_t>& mBytes;
};

std::optional<uint32_t> Link(const ModelImage& image, uint32_t address, const std::string& what) {
    if (address == 0) {
        return std::nullopt;
    }
    std::optional<uint32_t> offset = image.Resolve(address);
    if (!offset) {
        Fail(what + ": 0x" + Hex(address) + " is not an address inside the image");
    }
    return offset;
}

struct DlLayout {
    uint32_t primary;
    int32_t secondary; // -1 when the record has no second list
    uint32_t vertices;
    uint32_t count;
    uint32_t countBytes;
    bool countSigned;
};

const DlLayout* LayoutFor(uint32_t type) {
    static const DlLayout kDl = { 0x00, 0x04, 0x0C, 0x10, 2, false };
    static const DlLayout kCollision = { 0x00, 0x04, 0x08, 0x0C, 2, true };
    static const DlLayout kPrimary = { 0x08, -1, 0x04, 0x00, 4, true };
    switch (type) {
        case kNodeDl:
            return &kDl;
        case kNodeDlCollision:
            return &kCollision;
        case kNodeDlPrimary:
            return &kPrimary;
    }
    return nullptr;
}

DisplayListRecord ReadDisplayList(const ModelImage& image, const Bytes& bytes, uint32_t type, uint32_t record,
                                  const std::string& where) {
    const DlLayout& layout = *LayoutFor(type);
    uint32_t recordEnd = std::max(layout.vertices + kPointerSize, layout.count + layout.countBytes);
    if (size_t(record) + recordEnd > bytes.size()) {
        Fail(where + ": data record runs off the image");
    }
    DisplayListRecord out;
    out.verticesAddress = bytes.U32(record + layout.vertices);
    if (layout.secondary >= 0) {
        out.secondary = Link(image, bytes.U32(record + uint32_t(layout.secondary)), where + " secondary list");
    }
    int64_t count;
    if (layout.countBytes == 2) {
        count = layout.countSigned ? bytes.S16(record + layout.count) : int64_t(bytes.U16(record + layout.count));
    } else {
        count = bytes.S32(record + layout.count);
    }
    uint32_t perCount = type == kNodeDlPrimary ? kPrimaryOnlyVerticesPerCount : 1;
    out.primary = Link(image, bytes.U32(record + layout.primary), where + " primary list");
    out.vertices = Link(image, out.verticesAddress, where + " vertices");
    out.vertexBytes = uint32_t(std::max<int64_t>(count, 0) * perCount * kVertexBytes);
    return out;
}

ModelNode ReadNode(const ModelImage& image, const Bytes& bytes, uint32_t offset) {
    std::string where = NodeName(offset);
    if (size_t(offset) + kNodeSize > bytes.size()) {
        Fail(where + ": runs off the image");
    }
    ModelNode node;
    node.offset = offset;
    node.type = bytes.U16(offset) & 0xFF;
    node.data = Link(image, bytes.U32(offset + 0x04), where + " data");
    if (LayoutFor(node.type) != nullptr) {
        if (!node.data) {
            Fail(where + ": type 0x" + Hex(node.type) + " has no data record");
        }
        node.displayList = ReadDisplayList(image, bytes, node.type, *node.data, where);
    }
    node.parent = Link(image, bytes.U32(offset + 0x08), where + " parent");
    node.next = Link(image, bytes.U32(offset + 0x0C), where + " next");
    node.prev = Link(image, bytes.U32(offset + 0x10), where + " prev");
    node.child = Link(image, bytes.U32(offset + 0x14), where + " child");
    return node;
}

// Nodes a node reaches through its data record instead of (or as well as) its own child link. The game copies a LOD
// or switch node's target into its child link at load time, so the file's own link can be null. A BSP node's left
// and right pointers are not listed: they name the same nodes as its child link and that child's next link.
std::vector<uint32_t> ImpliedChildren(const ModelImage& image, const Bytes& bytes, const ModelNode& node) {
    if (!node.data) {
        return {};
    }
    uint32_t fieldAt;
    if (node.type == kNodeLod) {
        fieldAt = *node.data + 0x08;
    } else if (node.type == kNodeSwitch) {
        fieldAt = *node.data;
    } else {
        return {};
    }
    if (size_t(fieldAt) + kPointerSize > bytes.size()) {
        Fail(NodeName(node.offset) + ": data record runs off the image");
    }
    std::optional<uint32_t> target = Link(image, bytes.U32(fieldAt), NodeName(node.offset) + " data target");
    return target ? std::vector<uint32_t>{ *target } : std::vector<uint32_t>{};
}

std::optional<uint32_t> FirstChild(const ModelImage& image, const Bytes& bytes, const ModelNode& node) {
    if (node.child) {
        return node.child;
    }
    std::vector<uint32_t> targets;
    if (node.type == kNodeLod || node.type == kNodeSwitch) {
        targets = ImpliedChildren(image, bytes, node);
    }
    return targets.empty() ? std::nullopt : std::optional<uint32_t>(targets[0]);
}

// The node offset a pointer field of `node`'s data record names.
std::optional<uint32_t> LinkAt(const ModelImage& image, const Bytes& bytes, const ModelNode& node, uint32_t at) {
    uint32_t address = bytes.U32(*node.data + at);
    if (address == 0) {
        return std::nullopt;
    }
    std::optional<uint32_t> offset = image.Resolve(address);
    if (!offset) {
        Fail(NodeName(node.offset) + ": 0x" + Hex(address) + " is not an address inside the image");
    }
    return offset;
}

ModelTree ParseTree(const ModelImage& image, const ModelHeaderEntry& header) {
    Bytes bytes(image.data);
    ModelTree tree;
    size_t texturesAt = size_t(header.numSwitches) * kPointerSize;
    size_t root = texturesAt + size_t(header.numTextures) * kTextureEntrySize;
    if (header.numSwitches < 0 || header.numTextures < 0 || root + kNodeSize > image.data.size()) {
        Fail("switch array and texture table run past the image");
    }
    tree.root = uint32_t(root);

    for (int32_t i = 0; i < header.numSwitches; i++) {
        tree.switches.push_back(Link(image, bytes.U32(size_t(i) * kPointerSize), "switch " + std::to_string(i)));
    }
    for (size_t at = texturesAt; at < root; at += kTextureEntrySize) {
        ModelTexture t;
        t.textureId = bytes.U32(at);
        t.width = uint8_t(bytes.U8(at + 4));
        t.height = uint8_t(bytes.U8(at + 5));
        t.mipMapTiles = uint8_t(bytes.U8(at + 6));
        t.type = uint8_t(bytes.U8(at + 7));
        t.renderDepth = uint8_t(bytes.U8(at + 8));
        t.sFlags = uint8_t(bytes.U8(at + 9));
        t.tFlags = uint8_t(bytes.U8(at + 10));
        tree.textures.push_back(t);
    }

    std::vector<uint32_t> stack = { tree.root };
    std::set<uint32_t> queued = { tree.root };
    while (!stack.empty()) {
        uint32_t offset = stack.back();
        stack.pop_back();
        queued.erase(offset);
        ModelNode node = ReadNode(image, bytes, offset);
        std::vector<uint32_t> implied = ImpliedChildren(image, bytes, node);
        tree.nodes[offset] = node;
        // An implied child equal to the child link is the same edge.
        std::vector<std::optional<uint32_t>> follows = { node.next, node.child };
        for (uint32_t target : implied) {
            if (target != node.child) {
                follows.emplace_back(target);
            }
        }
        for (const std::optional<uint32_t>& follow : follows) {
            if (!follow) {
                continue;
            }
            if (tree.nodes.count(*follow) != 0 || queued.count(*follow) != 0) {
                Fail(NodeName(*follow) + " is reached twice");
            }
            stack.push_back(*follow);
            queued.insert(*follow);
        }
    }
    return tree;
}

std::optional<EffectRecord> EffectRecordOf(const ModelImage& image, const ModelTree& tree, const ModelNode& node) {
    if ((node.type != kNodeGunfire && node.type != kNodeShadow) || !node.data) {
        return std::nullopt;
    }
    Bytes bytes(image.data);
    bool gunfire = node.type == kNodeGunfire;
    size_t sizeAt = *node.data + (gunfire ? kGunfireSizeOffset : kShadowSizeOffset);
    size_t imageAt = *node.data + (gunfire ? kGunfireImageOffset : kShadowImageOffset);
    if (imageAt + kPointerSize > image.data.size() || sizeAt + 12 > image.data.size()) {
        Fail(NodeName(node.offset) + ": data record runs off the image");
    }
    EffectRecord record;
    record.size[0] = bytes.U32(sizeAt);
    record.size[1] = bytes.U32(sizeAt + 4);
    record.size[2] = gunfire ? bytes.U32(sizeAt + 8) : 0;

    uint32_t address = bytes.U32(imageAt);
    if (address == 0) {
        return record;
    }
    std::optional<uint32_t> offset = image.Resolve(address);
    size_t tableStart = tree.switches.size() * kPointerSize;
    if (!offset || *offset < tableStart || (*offset - tableStart) % kTextureEntrySize != 0) {
        Fail(NodeName(node.offset) + ": image 0x" + Hex(address) + " is not a texture table entry");
    }
    size_t index = (*offset - tableStart) / kTextureEntrySize;
    if (index >= tree.textures.size()) {
        Fail(NodeName(node.offset) + ": image 0x" + Hex(address) + " is past the texture table");
    }
    record.texture = tree.textures[index];
    return record;
}

// A display-list node's vertex array length.
uint32_t ModelExtent(const ModelImage& image, const DisplayListRecord& record) {
    if (record.vertexBytes != 0) {
        return record.vertexBytes;
    }
    // A count of zero cannot bound the array (one prop node stores 0 yet loads 16 vertices), so use the furthest
    // byte its lists load.
    Resolve resolve = image.DisplayListResolver(record.verticesAddress);
    int64_t end = 0;
    for (const std::optional<uint32_t>& start : { record.primary, record.secondary }) {
        if (!start) {
            continue;
        }
        DisplayListRefs refs = WalkDisplayList(image.data, *start, resolve);
        for (const auto& [offset, length] : MergeVertexRanges(refs)) {
            end = std::max<int64_t>(end, int64_t(offset) + length - *record.vertices);
        }
    }
    return uint32_t(end);
}

// Draw-order sort value inputs: the other node and the two points.
struct SortFields {
    std::optional<uint32_t> other;
    uint32_t point[3] = {};
    uint32_t point2[3] = {};
};

std::optional<SortFields> SortFieldsOf(const ModelImage& image, const Bytes& bytes, const ModelNode& node) {
    if (std::find(std::begin(kUnpackedSortNodeTypes), std::end(kUnpackedSortNodeTypes), node.type) !=
        std::end(kUnpackedSortNodeTypes)) {
        Fail(NodeName(node.offset) + ": type 0x" + Hex(node.type) + " has no draw-order data packed");
    }
    if (!node.data) {
        return std::nullopt;
    }
    SortFields fields;
    uint32_t data = *node.data;
    if (node.type == kNodeHeader) {
        fields.other = LinkAt(image, bytes, node, 0x04);
    } else if (node.type == kNodeGroup || node.type == kNodeGroupAlt) {
        fields.other = LinkAt(image, bytes, node, 0x14);
    } else if (node.type == kNodeGunfire) {
        for (uint32_t i = 0; i < 3; i++) {
            fields.point[i] = bytes.U32(data + 4 * i);
        }
    } else if (node.type == kNodeShadow) {
        fields.point[0] = bytes.U32(data);
        fields.point[2] = bytes.U32(data + 4);
        fields.other = LinkAt(image, bytes, node, 0x14);
    } else if (node.type == kNodeInterlink) {
        for (uint32_t i = 0; i < 3; i++) {
            fields.point[i] = bytes.U32(data + 4 * i);
            fields.point2[i] = bytes.U32(data + 0x0C + 4 * i);
        }
    } else {
        return std::nullopt;
    }
    return fields;
}

struct MatrixFields {
    uint32_t origin[3] = {};
    int32_t first = kNoIndex;
    int32_t bend = kNoIndex;
};

MatrixFields MatrixFieldsOf(const Bytes& bytes, const ModelNode& node) {
    MatrixFields fields;
    if (!node.data) {
        return fields;
    }
    uint32_t data = *node.data;
    if (node.type == kNodeHeader) {
        fields.first = bytes.S16(data + 0x02);
    } else if (node.type == kNodeGroup || node.type == kNodeGroupAlt) {
        for (uint32_t i = 0; i < 3; i++) {
            fields.origin[i] = bytes.U32(data + 4 * i);
        }
        fields.first = bytes.S16(data + 0x0E);
        fields.bend = bytes.S16(data + 0x10);
    } else if (node.type == kNodeGroupSimple) {
        for (uint32_t i = 0; i < 3; i++) {
            fields.origin[i] = bytes.U32(data + 4 * i);
        }
        fields.first = bytes.S16(data + 0x0C);
    }
    return fields;
}

// A display-list node's ModelType, the pre-defined render-mode and combiner setup its lists are drawn with; 0 for
// any other node type.
uint32_t RenderType(const Bytes& bytes, const ModelNode& node) {
    int32_t value;
    if (node.type == kNodeDl && node.data) {
        value = int8_t(bytes.U8(*node.data + 0x12));
    } else if (node.type == kNodeDlCollision && node.data) {
        value = bytes.S16(*node.data + 0x18);
    } else {
        return 0;
    }
    if (value < 0 || value > int32_t(kMaxRenderType)) {
        Fail(NodeName(node.offset) + ": unknown model type " + std::to_string(value));
    }
    return uint32_t(value);
}

// The bounding radius of a header, group or simple group node.
std::optional<uint32_t> JointRadius(const Bytes& bytes, const ModelNode& node) {
    if (!node.data) {
        return std::nullopt;
    }
    if (node.type == kNodeHeader) {
        return bytes.U32(*node.data + 0x08);
    }
    if (node.type == kNodeGroup || node.type == kNodeGroupAlt) {
        return bytes.U32(*node.data + 0x18);
    }
    if (node.type == kNodeGroupSimple) {
        return bytes.U32(*node.data + 0x10);
    }
    return std::nullopt;
}

// A group node's JointID, or a header node's AnimPart (the joint its root motion is read from).
std::optional<uint32_t> JointId(const Bytes& bytes, const ModelNode& node) {
    if (!node.data) {
        return std::nullopt;
    }
    if (node.type == kNodeHeader) {
        return bytes.U16(*node.data);
    }
    if (node.type == kNodeGroup || node.type == kNodeGroupAlt) {
        return bytes.U16(*node.data + 0x0C);
    }
    return std::nullopt;
}

std::vector<uint32_t> Preorder(const ModelImage& image, const Bytes& bytes, const ModelTree& tree) {
    std::vector<uint32_t> order;
    std::vector<uint32_t> stack = { tree.root };
    while (!stack.empty()) {
        uint32_t offset = stack.back();
        stack.pop_back();
        order.push_back(offset);
        const ModelNode& node = tree.nodes.at(offset);
        // Push the sibling first so the child is visited before it.
        if (node.next) {
            stack.push_back(*node.next);
        }
        if (std::optional<uint32_t> child = FirstChild(image, bytes, node)) {
            stack.push_back(*child);
        }
    }
    return order;
}

int32_t IndexOf(const std::map<uint32_t, int32_t>& indexOf, const std::optional<uint32_t>& node) {
    return node ? indexOf.at(*node) : kNoIndex;
}

void RequireInTree(const std::map<uint32_t, int32_t>& indexOf, const std::optional<uint32_t>& node, uint32_t owner,
                   const char* what) {
    if (node && indexOf.count(*node) == 0) {
        Fail(NodeName(owner) + ": " + what + " 0x" + Hex(*node) + " is not a node of the tree");
    }
}

// The collision table entry of a collision display-list node (0x18), after its record index.
void AppendCollisionRecord(const ModelImage& image, const Bytes& bytes, const std::map<uint32_t, int32_t>& indexOf,
                           const ModelNode& node, Out& out) {
    uint32_t data = *node.data;
    int32_t vertexCount = bytes.S16(data + 0x0C);
    int32_t collisionCount = bytes.S16(data + 0x0E);
    std::optional<uint32_t> verticesAt = LinkAt(image, bytes, node, 0x10);
    std::optional<uint32_t> usageAt = LinkAt(image, bytes, node, 0x14);
    if (!verticesAt || !usageAt || vertexCount < 0 || collisionCount < 0) {
        Fail(NodeName(node.offset) + ": collision display list has no collision table");
    }
    if (size_t(*verticesAt) + size_t(collisionCount) * 16 > image.data.size() ||
        size_t(*usageAt) + size_t(vertexCount) * 2 > image.data.size()) {
        Fail(NodeName(node.offset) + ": collision table runs off the file");
    }
    out.U16(uint32_t(vertexCount));
    out.U16(uint32_t(collisionCount));
    for (int32_t i = 0; i < collisionCount; i++) {
        size_t at = size_t(*verticesAt) + size_t(i) * 16;
        uint32_t relatedAddress = bytes.U32(at + 8);
        std::optional<uint32_t> related = relatedAddress != 0 ? image.Resolve(relatedAddress) : std::nullopt;
        if (relatedAddress != 0 && (!related || indexOf.count(*related) == 0)) {
            Fail(NodeName(node.offset) + ": related node 0x" + Hex(relatedAddress) + " is not a node of the tree");
        }
        out.U16(bytes.U16(at));
        out.U16(bytes.U16(at + 2));
        out.U16(bytes.U16(at + 4));
        out.U16(bytes.U16(at + 6));
        out.S32(IndexOf(indexOf, related));
        out.U16(bytes.U16(at + 12));
        out.Pad(2);
    }
    for (int32_t i = 0; i < vertexCount; i++) {
        out.U16(bytes.U16(*usageAt + size_t(i) * 2));
    }
}

std::vector<uint8_t> EncodeTree(const ModelImage& image, const ModelTree& tree, const TextureLookup& textures,
                                float radius) {
    Bytes bytes(image.data);
    std::vector<uint32_t> order = Preorder(image, bytes, tree);
    std::map<uint32_t, int32_t> indexOf;
    for (size_t i = 0; i < order.size(); i++) {
        indexOf[order[i]] = int32_t(i);
    }
    if (indexOf.size() != tree.nodes.size()) {
        Fail("node tree has nodes the child and next links do not reach");
    }

    std::map<uint32_t, uint32_t> parents;
    for (uint32_t offset : order) {
        std::optional<uint32_t> child = FirstChild(image, bytes, tree.nodes.at(offset));
        while (child) {
            parents[*child] = offset;
            child = tree.nodes.at(*child).next;
        }
    }

    int32_t highestMatrix = -1;
    for (const auto& [offset, node] : tree.nodes) {
        MatrixFields fields = MatrixFieldsOf(bytes, node);
        highestMatrix = std::max({ highestMatrix, fields.first, fields.bend });
    }

    std::vector<uint8_t> payload, lodTable, bspTable, jointTable, sortTable, effectTable, boundsTable, hitTable,
        collisionTable, sizeTable;
    Out out(payload), lod(lodTable), bsp(bspTable), joint(jointTable), sort(sortTable), effect(effectTable),
        bounds(boundsTable), hit(hitTable), collision(collisionTable), size(sizeTable);
    uint32_t lodCount = 0, bspCount = 0, jointCount = 0, sortCount = 0, effectCount = 0, boundsCount = 0, hitCount = 0,
             collisionCount = 0, sizeCount = 0;

    out.U32(uint32_t(order.size()));
    out.U32(uint32_t(highestMatrix + 1));

    for (size_t i = 0; i < order.size(); i++) {
        uint32_t offset = order[i];
        const ModelNode& node = tree.nodes.at(offset);
        MatrixFields matrix = MatrixFieldsOf(bytes, node);
        std::optional<uint32_t> child = FirstChild(image, bytes, node);
        const std::optional<DisplayListRecord>& record = node.displayList;
        out.U32(offset);
        out.U8(node.type);
        out.U8(RenderType(bytes, node));
        out.U16(uint32_t(matrix.first));
        out.U16(uint32_t(matrix.bend));
        out.Pad(2);
        out.S32(IndexOf(indexOf, child));
        out.S32(IndexOf(indexOf, node.next));
        out.S32(parents.count(offset) != 0 ? indexOf.at(parents.at(offset)) : kNoIndex);
        for (uint32_t word : matrix.origin) {
            out.U32(word);
        }
        out.U32(record && record->primary ? *record->primary : kNoList);
        out.U32(record && record->secondary ? *record->secondary : kNoList);

        if (node.type == kNodeLod && node.data) {
            lod.U32(uint32_t(i));
            lod.U32(bytes.U32(*node.data));
            lod.U32(bytes.U32(*node.data + 4));
            lodCount++;
        }
        if (node.type == kNodeBsp && node.data) {
            uint32_t rightAddress = bytes.U32(*node.data + 0x1C);
            std::optional<uint32_t> rightChild = rightAddress != 0 ? image.Resolve(rightAddress) : std::nullopt;
            bsp.U32(uint32_t(i));
            for (uint32_t k = 0; k < 6; k++) {
                bsp.U32(bytes.U32(*node.data + 4 * k));
            }
            bsp.S32(bytes.S16(*node.data + 0x20));
            bsp.S32(rightChild ? indexOf.at(*rightChild) : kNoIndex);
            bspCount++;
        }
        if (std::optional<uint32_t> id = JointId(bytes, node)) {
            joint.U32(uint32_t(i));
            joint.U16(*id);
            jointCount++;
        }
        if (std::optional<SortFields> fields = SortFieldsOf(image, bytes, node)) {
            RequireInTree(indexOf, fields->other, offset, "linked node");
            sort.U32(uint32_t(i));
            sort.S32(IndexOf(indexOf, fields->other));
            for (uint32_t word : fields->point) {
                sort.U32(word);
            }
            for (uint32_t word : fields->point2) {
                sort.U32(word);
            }
            sortCount++;
        }
        if (node.type == kNodeBoundingBox && node.data) {
            if (size_t(*node.data) + 28 > image.data.size()) {
                Fail(NodeName(offset) + ": bounding box runs off the file");
            }
            bounds.U32(uint32_t(i));
            for (uint32_t k = 0; k < 6; k++) {
                bounds.U32(bytes.U32(*node.data + 4 + 4 * k));
            }
            boundsCount++;
        }
        if (std::optional<uint32_t> jointRadius = JointRadius(bytes, node)) {
            size.U32(uint32_t(i));
            size.U32(*jointRadius);
            sizeCount++;
        }
        if ((node.type == kNodeBoundingBox || node.type == kNodeHitSphere) && node.data) {
            uint32_t flags = bytes.U16(offset) & 0xFF00;
            hit.U32(uint32_t(i));
            hit.S32(bytes.S32(*node.data));
            if (node.type == kNodeBoundingBox) {
                hit.U32(0);
                hit.Pad(12);
                hit.S32(kNoIndex);
                hit.U32(0);
                hit.U32(0);
            } else {
                hit.U32(bytes.U32(*node.data + 0x04));
                for (uint32_t k = 0; k < 3; k++) {
                    hit.U32(bytes.U32(*node.data + 0x08 + 4 * k));
                }
                std::optional<uint32_t> other = LinkAt(image, bytes, node, 0x14);
                RequireInTree(indexOf, other, offset, "linked node");
                hit.S32(IndexOf(indexOf, other));
                hit.U32(bytes.U32(*node.data + 0x18));
                hit.U32(bytes.U32(*node.data + 0x1C));
            }
            hit.U16(flags);
            hit.Pad(2);
            hitCount++;
        }
        if (node.type == kNodeDlCollision && node.data) {
            collision.U32(uint32_t(i));
            AppendCollisionRecord(image, bytes, indexOf, node, collision);
            collisionCount++;
        }
        std::optional<EffectRecord> effectRecord = EffectRecordOf(image, tree, node);
        if (effectRecord) {
            const std::optional<ModelTexture>& texture = effectRecord->texture;
            effect.U32(uint32_t(i));
            for (uint32_t word : effectRecord->size) {
                effect.U32(word);
            }
            uint32_t format = 0;
            if (texture) {
                TextureBinding binding;
                if (Error err = textures(texture->textureId, binding); !err.ok()) {
                    Fail(err.message);
                }
                format = binding.fmt;
            }
            effect.U8(texture ? 1 : 0);
            effect.U8(texture ? texture->width : 0);
            effect.U8(texture ? texture->height : 0);
            effect.U8(texture ? texture->mipMapTiles : 0);
            effect.U8(format);
            effect.Pad(3);
            effectCount++;
        }
    }

    auto section = [&](uint32_t count, const std::vector<uint8_t>& table) {
        out.U32(count);
        payload.insert(payload.end(), table.begin(), table.end());
    };
    section(lodCount, lodTable);
    section(bspCount, bspTable);
    section(jointCount, jointTable);
    out.U32(uint32_t(tree.switches.size()));
    for (const std::optional<uint32_t>& target : tree.switches) {
        if (target && indexOf.count(*target) == 0) {
            Fail("switch array entry 0x" + Hex(*target) + " is not a node of the tree");
        }
        out.S32(IndexOf(indexOf, target));
    }
    section(sortCount, sortTable);
    section(effectCount, effectTable);
    section(boundsCount, boundsTable);
    section(hitCount, hitTable);
    section(collisionCount, collisionTable);
    section(sizeCount, sizeTable);
    uint32_t radiusWord;
    std::memcpy(&radiusWord, &radius, sizeof(radiusWord));
    out.U32(radiusWord);
    return payload;
}

} // namespace

Error ParseModelTree(const ModelImage& image, const ModelHeaderEntry& header, ModelTree& tree) {
    try {
        tree = ParseTree(image, header);
    } catch (const ModelFail& fail) { return ToError(fail); }
    return {};
}

Error ReadEffectRecord(const ModelImage& image, const ModelTree& tree, const ModelNode& node,
                       std::optional<EffectRecord>& record) {
    try {
        record = EffectRecordOf(image, tree, node);
    } catch (const ModelFail& fail) { return ToError(fail); }
    return {};
}

Error FindModelVertexBlocks(const ModelImage& image, const ModelTree& tree, std::vector<ModelVertexBlock>& blocks) {
    blocks.clear();
    try {
        for (const auto& [offset, node] : tree.nodes) {
            const std::optional<DisplayListRecord>& record = node.displayList;
            if (!record || !record->vertices) {
                continue;
            }
            uint32_t length = ModelExtent(image, *record);
            if (uint64_t(*record->vertices) + length > image.data.size()) {
                Fail(NodeName(offset) + ": vertex array runs off the image");
            }
            if (length % kVertexBytes != 0) {
                Fail(NodeName(offset) + ": vertex array is not a whole number of vertices");
            }
            blocks.push_back({ offset, *record->vertices, length });
        }
    } catch (const ModelFail& fail) { return ToError(fail); }
    return {};
}

Error EncodeModelTreePayload(const ModelImage& image, const ModelTree& tree, const TextureLookup& textures,
                             float radius, std::vector<uint8_t>& payload) {
    try {
        payload = EncodeTree(image, tree, textures, radius);
    } catch (const ModelFail& fail) { return ToError(fail); }
    return {};
}

std::vector<uint8_t> EncodeSkeletonPayload(std::span<const SkeletonJoint> joints) {
    std::vector<uint8_t> payload;
    Out out(payload);
    out.U32(uint32_t(joints.size()));
    for (const SkeletonJoint& joint : joints) {
        out.U16(joint.nodeType);
        out.U16(joint.matrixA);
        out.U16(joint.matrixB);
    }
    return payload;
}

uint32_t EffectImageRequest(const ModelTexture& texture) {
    uint32_t kind = texture.mipMapTiles != 0 ? kRequestTypeMipmapValue : kRequestTypeTilePreswapped;
    return (kG_SETTEX << 24) | (RequestWrapMode(texture.sFlags) << 22) | (RequestWrapMode(texture.tFlags) << 20) |
           (kRequestSampleOffset << 18) | kind;
}

} // namespace Frigate::Extract
