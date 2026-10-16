// Tests for the model, level geometry and level data packers. The container, tree and stan checks run on hand-built
// inputs (the tree and display-list expectations were produced by the Python packers on the same bytes); the stage
// checks run on a synthetic ROM; the last check runs the whole set against a real US ROM when FRIGATE_ROMS (a
// path-separator list of ROM files and directories) points at one.

#include "extract/DisplayListEmit.h"
#include "extract/Extractor.h"
#include "extract/Inflate.h"
#include "extract/ModelContainer.h"
#include "extract/ModelPackers.h"
#include "extract/ModelTables.h"
#include "extract/ModelTree.h"
#include "extract/Stan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

std::vector<uint8_t> FromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

void Put32(std::vector<uint8_t>& data, size_t at, uint32_t value) {
    data[at] = uint8_t(value >> 24);
    data[at + 1] = uint8_t(value >> 16);
    data[at + 2] = uint8_t(value >> 8);
    data[at + 3] = uint8_t(value);
}

void Put16(std::vector<uint8_t>& data, size_t at, uint32_t value) {
    data[at] = uint8_t(value >> 8);
    data[at + 1] = uint8_t(value);
}

uint32_t Word(const std::vector<uint8_t>& data, size_t at) {
    return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) | (uint32_t(data[at + 2]) << 8) | data[at + 3];
}

uint64_t Word64(const std::vector<uint8_t>& data, size_t at) {
    return (uint64_t(Word(data, at)) << 32) | Word(data, at + 4);
}

constexpr size_t kPayload = kOtrHeaderSize; // a member's payload follows the 64-byte resource header

// --- a hand-built model -----------------------------------------------------------------------------------------

constexpr uint32_t kSeg5 = 0x05000000;

// One switch, one texture, a header node with a display-list child; the display list loads two vertices.
std::vector<uint8_t> BuildModelImage() {
    std::vector<uint8_t> img(0xC0, 0);
    Put32(img, 0x00, kSeg5 + 0x28); // switch 0: the display-list node
    Put32(img, 0x04, 7);            // texture 0: image 7, 16x16
    img[8] = 16, img[9] = 16;
    auto node = [&](size_t at, uint32_t type, uint32_t data, uint32_t parent, uint32_t next, uint32_t child) {
        Put16(img, at, type);
        Put32(img, at + 4, data);
        Put32(img, at + 8, parent);
        Put32(img, at + 0xC, next);
        Put32(img, at + 0x14, child);
    };
    node(0x10, 0x01, kSeg5 + 0x58, 0, 0, kSeg5 + 0x28);
    node(0x28, 0x04, kSeg5 + 0x70, kSeg5 + 0x10, 0, 0);
    Put16(img, 0x58, 3);                  // header record: AnimPart
    Put32(img, 0x58 + 8, 0x3F800000);     // radius 1.0
    Put32(img, 0x70, kSeg5 + 0x90);       // display-list record: primary list
    Put32(img, 0x70 + 0xC, kSeg5 + 0xA0); // vertices
    Put16(img, 0x70 + 0x10, 2);           // vertex count
    img[0x70 + 0x12] = 1;                 // model type
    Put32(img, 0x90, 0x04000020);         // G_VTX 2 vertices from segment 4
    Put32(img, 0x94, 0x04000000);
    Put32(img, 0x98, 0xB8000000); // G_ENDDL
    for (int i = 0; i < 32; i++) {
        img[0xA0 + i] = uint8_t(i + 1);
    }
    return img;
}

const ModelHeader kTestHeader = { "test", 1, 1, nullptr, 2.5 };

void TestModelTree() {
    ModelImage image;
    image.data = BuildModelImage();
    ModelTree tree;
    Error err = ParseModelTree(image, kTestHeader, tree);
    Check(err.ok(), "a model tree parses");
    Check(tree.root == 0x10 && tree.nodes.size() == 2, "root and two nodes");
    Check(tree.switches.size() == 1 && tree.switches[0] == 0x28, "the switch array");
    Check(tree.textures.size() == 1 && tree.textures[0].textureId == 7 && tree.textures[0].width == 16,
          "the texture table");
    Check(tree.nodes[0x10].child == 0x28 && tree.nodes[0x28].parent == 0x10, "links");
    const DisplayListRecord& record = *tree.nodes[0x28].displayList;
    Check(record.primary == 0x90 && !record.secondary && record.vertices == 0xA0 && record.vertexBytes == 32,
          "the display-list record");

    std::vector<ModelVertexBlock> blocks;
    Check(FindModelVertexBlocks(image, tree, blocks).ok() && blocks.size() == 1 && blocks[0].node == 0x28 &&
              blocks[0].offset == 0xA0 && blocks[0].length == 32,
          "one vertex block per display-list node");

    std::vector<uint8_t> payload;
    err = EncodeModelTreePayload(
        image, tree, [](uint32_t, TextureBinding&) { return Error{}; }, static_cast<float>(kTestHeader.radius), payload);
    Check(err.ok(), "the tree encodes");
    // Produced by tools/o2r_packer/ge_modeltree.py on the same image.
    Check(payload == FromHex("00000002000000010000001001000000ffff000000000001ffffffffffffffff000000000000000000000000"
                             "ffffffffffffffff000000280401ffffffff0000ffffffffffffffff00000000000000000000000000000000"
                             "00000090ffffffff00000000000000000000000100000000000300000001000000010000000100000000ffff"
                             "ffff000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                             "0001000000003f80000040200000"),
          "the tree payload matches the Python packer's");

    // The display-list node's lists, with G_VTX addresses bound to its own array.
    Resolve resolve = image.DisplayListResolver(record.verticesAddress);
    DisplayListRefs refs = WalkDisplayList(image.data, 0x90, resolve);
    Check(refs.unresolved.empty() && refs.vertices.size() == 1 && refs.vertices[0].offset == 0xA0 &&
              refs.vertices[0].count == 2,
          "the walk binds segment 4 to the node's array");
}

void TestModelTreeErrors() {
    {
        ModelImage image;
        image.data = BuildModelImage();
        Put32(image.data, 0x10 + 0x0C, kSeg5 + 0x28); // next and child name the same node
        ModelTree tree;
        Check(ParseModelTree(image, kTestHeader, tree).code == ErrorCode::Corrupt, "a node reached twice");
    }
    {
        ModelImage image;
        image.data = BuildModelImage();
        Put32(image.data, 0x10 + 0x14, kSeg5 + 0x1000);
        ModelTree tree;
        Check(ParseModelTree(image, kTestHeader, tree).code == ErrorCode::Corrupt, "a link outside the image");
    }
    {
        ModelImage image;
        image.data = BuildModelImage();
        ModelHeader big = kTestHeader;
        big.numTextures = 100;
        ModelTree tree;
        Check(ParseModelTree(image, big, tree).code == ErrorCode::Corrupt, "a texture table past the image");
    }
    {
        ModelImage image;
        image.data = BuildModelImage();
        Put16(image.data, 0x70 + 0x10, 0); // a count of zero loads no array...
        ModelTree tree;
        std::vector<ModelVertexBlock> blocks;
        Check(ParseModelTree(image, kTestHeader, tree).ok() && FindModelVertexBlocks(image, tree, blocks).ok() &&
                  blocks.size() == 1 && blocks[0].length == 32,
              "...so the array is sized by what the lists load");
    }
}

void TestSkeleton() {
    const SkeletonJoint joints[] = { { 1, 2, 3 }, { 0x10, 0xFFFF, 4 } };
    Check(EncodeSkeletonPayload(joints) == FromHex("00000002"
                                                   "000100020003"
                                                   "0010ffff0004"),
          "skeleton payload");
    bool found = false;
    std::span<const SkeletonJoint> guard = FindSkeleton("guard", &found);
    Check(found && !guard.empty(), "a known skeleton");
    FindSkeleton("nonexistent", &found);
    Check(!found, "an unknown skeleton");
    const ModelHeader* header = FindModelHeader("armourguard");
    Check(header != nullptr && header->numSwitches == 7 && header->numTextures == 17 &&
              std::string(header->skeleton) == "guard",
          "a model header row");
    Check(FindModelHeader("nonexistent") == nullptr, "an unknown model header");
}

// --- containers -------------------------------------------------------------------------------------------------

std::vector<uint8_t> Compressed(const std::vector<uint8_t>& plain) {
    std::vector<uint8_t> deflated;
    DeflateRaw(plain, deflated, 9);
    std::vector<uint8_t> out = { 0x11, 0x72 };
    out.insert(out.end(), deflated.begin(), deflated.end());
    return out;
}

// A bg file with one room: its vertex blob is two vertices, its primary list loads them.
std::vector<uint8_t> BuildBgFile(bool withSecondary) {
    std::vector<uint8_t> vertices(32);
    for (int i = 0; i < 32; i++) {
        vertices[i] = uint8_t(0x40 + i);
    }
    std::vector<uint8_t> primary(16, 0);
    Put32(primary, 0, 0x04000020);
    Put32(primary, 4, 0x0E000000);
    Put32(primary, 8, 0xB8000000);
    std::vector<uint8_t> secondary(8, 0);
    Put32(secondary, 0, 0xB8000000);

    std::vector<uint8_t> file(0x80, 0);
    Put32(file, 4, 0x0F000014); // the room table
    std::vector<uint8_t> blobs[3] = { Compressed(vertices), Compressed(primary), Compressed(secondary) };
    uint32_t at[3] = {};
    for (int i = 0; i < 3; i++) {
        at[i] = uint32_t(file.size());
        file.insert(file.end(), blobs[i].begin(), blobs[i].end());
        while (file.size() % 4 != 0) {
            file.push_back(0);
        }
    }
    // Entry 0 is a placeholder; entry 1 is the room; entry 2 holds the end pointers; entry 3 ends the table.
    Put32(file, 0x14 + 24, 0x0F000000 + at[0]);
    Put32(file, 0x14 + 24 + 4, 0x0F000000 + at[1]);
    Put32(file, 0x14 + 24 + 8, withSecondary ? 0x0F000000 + at[2] : 0);
    Put32(file, 0x14 + 48, 0x0F000000 + uint32_t(file.size()));
    Put32(file, 0x14 + 48 + 4, 0x0F000001);
    return file;
}

void TestBgContainer() {
    std::vector<uint8_t> file = BuildBgFile(true);
    BgFile bg;
    Error err = BgFile::Parse(file, bg);
    Check(err.ok() && bg.rooms.size() == 1, "a bg file with one room");
    Check(bg.header.size() == 0x80, "the header region ends at the first vertex blob, rounded up to 16");
    BgRoom room;
    Check(bg.InflateRoom(0, room).ok() && room.index == 1 && room.vertices.size() == 32 && room.primary.size() == 16 &&
              room.hasSecondary && room.secondary.size() == 8 && room.vertices[0] == 0x40,
          "a room's blobs inflate");
    Check(room.Resolve(0x0E000010) == 0x10 && !room.Resolve(0x0E000020) && !room.Resolve(0x05000000),
          "a room resolves only segment 0x0e addresses inside its vertex blob");

    file = BuildBgFile(false);
    Check(BgFile::Parse(file, bg).ok() && bg.InflateRoom(0, room).ok() && !room.hasSecondary,
          "a room with no secondary list");

    Put32(file, 4, 0);
    Check(BgFile::Parse(file, bg).code == ErrorCode::Corrupt, "no room table");
    Check(BgFile::Parse(std::vector<uint8_t>(4, 0), bg).code == ErrorCode::Corrupt, "a file shorter than its header");
    file = BuildBgFile(true);
    Put32(file, 0x14 + 24, 0);
    Check(BgFile::Parse(file, bg).code == ErrorCode::Corrupt, "no vertex blob to size the header by");
    file = BuildBgFile(true);
    Put32(file, 0x14 + 24 + 4, 0x0E000000);
    Check(BgFile::Parse(file, bg).code == ErrorCode::Corrupt, "a pointer outside segment 0x0f");

    ModelImage image;
    std::vector<uint8_t> plain = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Check(UnwrapModelFile(Compressed(plain), image).ok() && image.data == plain, "a model file inflates");
    Check(image.Resolve(kSeg5 + 4) == 4 && !image.Resolve(kSeg5 + 8) && !image.Resolve(0x06000000),
          "a model image resolves segment 5 addresses inside it");
    Resolve resolve = image.DisplayListResolver(kSeg5 + 2);
    Check(resolve(0x04000004) == 6 && resolve(0x03000040) == 0x40 && resolve(kSeg5 + 3) == 3,
          "a node's lists see segment 4 as its array and segment 3 as bare offsets");
}

// --- stan -------------------------------------------------------------------------------------------------------

// Two triangles linked to each other, then the end of the list.
std::vector<uint8_t> BuildStanFile() {
    const uint32_t first = 0x10;
    std::vector<uint8_t> file(first + 32 * 2 + 8, 0);
    Put32(file, 4, first);
    auto tile = [&](uint32_t at, uint32_t idRoom, uint32_t linkA) {
        Put32(file, at, idRoom);
        Put16(file, at + 4, 0x0123);
        Put16(file, at + 6, 0x3012);
        for (uint32_t i = 0; i < 3; i++) {
            Put16(file, at + 8 + 8 * i, 10 * i);
            Put16(file, at + 8 + 8 * i + 2, 20 * i);
            Put16(file, at + 8 + 8 * i + 4, 30 * i);
        }
        Put16(file, at + 8 + 6, linkA);
    };
    tile(first, 0x00012301, ((32 + 0x80) >> 3));
    tile(first + 32, 0x00012402, ((0 + 0x80) >> 3));
    return file;
}

void TestStan() {
    std::vector<uint8_t> payload;
    Check(ConvertStanFile(BuildStanFile(), payload).ok(), "a stan file converts");
    Check(payload.size() == 4 + 2 * 32 && Word(payload, 0) == 2, "two tiles, as the file stores them");
    Check(Word(payload, 4) == 0x00012301 && Word(payload, 8) == 0x01233012, "the tile header is kept");
    // Point 0's link names the other tile by index; the others had no neighbour.
    Check(payload[4 + 8 + 6] == 0 && payload[4 + 8 + 7] == 1 && payload[4 + 16 + 6] == 0xFF &&
              payload[4 + 16 + 7] == 0xFF,
          "links become tile indices, and no link becomes 0xFFFF");
    Check(payload[4 + 32 + 8 + 6] == 0 && payload[4 + 32 + 8 + 7] == 0, "the second tile links back to the first");

    std::vector<uint8_t> file = BuildStanFile();
    Put16(file, 0x10 + 8 + 6, 0x7F0); // a link to an address that holds no tile
    Check(ConvertStanFile(file, payload).code == ErrorCode::Corrupt, "a link to no tile");
    file = BuildStanFile();
    Put16(file, 0x10 + 6, 0x2012);
    Check(ConvertStanFile(file, payload).code == ErrorCode::Corrupt, "a tile with two points");
    file = BuildStanFile();
    file.resize(0x10 + 32);
    Check(ConvertStanFile(file, payload).code == ErrorCode::Corrupt, "a tile list that never ends");
    Check(ConvertStanFile(std::vector<uint8_t>(4, 0), payload).code == ErrorCode::Corrupt,
          "a file shorter than its header");
}

// --- stages -----------------------------------------------------------------------------------------------------

std::map<std::string, std::vector<uint8_t>> ReadZip(const fs::path& path, bool& ok) {
    std::map<std::string, std::vector<uint8_t>> members;
    ok = false;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t at) { return uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8); };
    auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    if (data.size() < 22 || u32(data.size() - 22) != 0x06054B50) {
        return members;
    }
    size_t eocd = data.size() - 22;
    size_t count = u16(eocd + 10);
    size_t at = u32(eocd + 16);
    for (size_t i = 0; i < count; i++) {
        if (u32(at) != 0x02014B50) {
            return members;
        }
        uint32_t method = u16(at + 10), crc = u32(at + 16), csize = u32(at + 20), size = u32(at + 24);
        size_t nameLength = u16(at + 28), extraLength = u16(at + 30), commentLength = u16(at + 32);
        size_t local = u32(at + 42);
        std::string name(data.begin() + at + 46, data.begin() + at + 46 + nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        size_t bodyAt = local + 30 + u16(local + 26) + u16(local + 28);
        std::span<const uint8_t> body(data.data() + bodyAt, csize);
        std::vector<uint8_t> bytes;
        if (method == 8) {
            if (!InflateRaw(body, bytes, size).ok() && size != 0) {
                return members;
            }
        } else {
            bytes.assign(body.begin(), body.end());
        }
        if (bytes.size() != size || Crc32(bytes) != crc) {
            return members;
        }
        members[name] = bytes;
    }
    ok = true;
    return members;
}

fs::path ScratchDir(const char* name) {
    fs::path dir = fs::temp_directory_path() / (std::string("frigate_extract_model_test_") + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// Runs one stage over `rom` and returns what it wrote.
std::map<std::string, std::vector<uint8_t>> RunStage(const std::function<Error(StageContext&, O2rWriter&)>& stage,
                                                     const std::vector<uint8_t>& rom, Region region, Error& error) {
    fs::path dir = ScratchDir("stage");
    O2rWriter writer;
    writer.Open(dir / "out.o2r");
    StageContext context(region, rom, nullptr, nullptr);
    error = stage(context, writer);
    writer.Close();
    bool ok = false;
    auto members = ReadZip(dir / "out.o2r", ok);
    fs::remove_all(dir);
    return members;
}

void TestWaterLists() {
    std::vector<uint8_t> rom(kRomSize, 0);
    Error error;
    auto members = RunStage(PackWaterLists, rom, Region::Us, error);
    Check(error.ok() && members.size() == 2, "two water lists");
    for (const char* name : { "bg/water/1508", "bg/water/1511" }) {
        auto it = members.find(name);
        Check(it != members.end(), name);
        if (it == members.end()) {
            continue;
        }
        const auto& m = it->second;
        Check(m.size() == kPayload + 8 + 13 * 8 && m[kPayload] == kUcodeIndyGe, "a water list's size and ucode byte");
        Check(Word(m, kPayload + 8 + 16) == 0xF2002002 && Word(m, kPayload + 8 + 4 * 8) == 0xFA00000F &&
                  Word(m, m.size() - 8) == 0xB8000000,
              "tile sizes, primitive colour and the end");
    }
    Check(Word(members["bg/water/1508"], kPayload + 8) == 0xF5800800 &&
              Word(members["bg/water/1511"], kPayload + 8) == 0xF5480400,
          "each list sets up its own tile format");
}

void TestStagesOnSyntheticRom() {
    const RomLayout& layout = GetRomLayout(Region::Us);
    const FileEntry* bgEntry = layout.FindFile("assets/obseg/bg/bg_ame_all_p.bin");
    Check(bgEntry != nullptr && !bgEntry->compressed, "the layout has an uncompressed bg file");
    if (bgEntry == nullptr) {
        return;
    }

    std::vector<uint8_t> rom(kRomSize, 0);
    std::vector<uint8_t> bg = BuildBgFile(true);
    std::copy(bg.begin(), bg.end(), rom.begin() + bgEntry->offset);
    Error error;
    auto members = RunStage(PackBgRooms, rom, Region::Us, error);
    Check(error.ok() && members.size() == 4, "the header, a vertex block and two lists; empty files are skipped");
    Check(members.count("bg/bg_ame/header") && members["bg/bg_ame/header"].size() == kPayload + 4 + 0x80,
          "the header blob");
    if (members.count("bg/bg_ame/room01/vtx/000000")) {
        const auto& m = members["bg/bg_ame/room01/vtx/000000"];
        Check(m.size() == kPayload + 4 + 32 && Word(m, kPayload) == 2 && m[kPayload + 4] == 0x40, "the vertex block");
    } else {
        Check(false, "room01 has a vertex block");
    }
    if (members.count("bg/bg_ame/room01/dl/primary/000000")) {
        const auto& m = members["bg/bg_ame/room01/dl/primary/000000"];
        Check(m.size() == kPayload + 8 + 16 + 8 && m[kPayload] == kUcodeIndyGe, "the primary list");
        Check(Word(m, kPayload + 8) == ((kOtrG_VTX_HASH << 24) | (2 << 12) | (2 << 1)) && Word(m, kPayload + 12) == 0 &&
                  Word64(m, kPayload + 16) == PathHash("bg/bg_ame/room01/vtx/000000"),
              "a vertex load names its block by path hash");
    } else {
        Check(false, "room01 has a primary list");
    }
    Check(members.count("bg/bg_ame/room01/dl/secondary/000000") == 1, "the secondary list");

    // Another region has no mapped model or level data yet.
    std::vector<uint8_t> blank(kRomSize, 0);
    RunStage(PackModels, blank, Region::Eur, error);
    Check(error.code == ErrorCode::RegionUnsupported, "models are refused for a region that is not mapped");
    RunStage(PackBgRooms, blank, Region::Eur, error);
    Check(error.code == ErrorCode::RegionUnsupported, "level geometry is refused for a region that is not mapped");
}

// --- real ROM ---------------------------------------------------------------------------------------------------

std::optional<fs::path> FindUsRom() {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(separator, start);
        if (end == std::string::npos) {
            end = list.size();
        }
        fs::path location = list.substr(start, end - start);
        start = end + 1;
        std::error_code ec;
        if (fs::is_regular_file(location, ec)) {
            if (IsUsRom(location)) {
                return location;
            }
        } else if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                if (entry.is_regular_file() && IsUsRom(entry.path())) {
                    return entry.path();
                }
            }
        }
    }
    return std::nullopt;
}

// Member sizes and CRCs of the Python packers' output for the US ROM (tools/o2r_packer/build_*_o2r.py).
struct Golden {
    const char* name;
    size_t size;
    uint32_t crc;
};

size_t CountPrefix(const std::map<std::string, std::vector<uint8_t>>& members, const std::string& prefix) {
    size_t n = 0;
    for (const auto& [name, bytes] : members) {
        n += name.compare(0, prefix.size(), prefix) == 0;
    }
    return n;
}

void TestRealRom() {
    std::optional<fs::path> rom = FindUsRom();
    if (!rom) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    fs::path dir = ScratchDir("rom");
    Extractor extractor;
    RegisterModelPackers(extractor);
    Options options;
    options.romPath = *rom;
    options.outputDir = dir;
    Result result = extractor.Run(options);
    Check(result.error.ok(), "model packers run on the US ROM");
    if (!result.error.ok()) {
        std::fprintf(stderr, "%s\n", result.error.message.c_str());
        return;
    }

    bool ok = false;
    auto objects = ReadZip(dir / "goldeneye_objects.o2r", ok);
    Check(ok && objects.size() == 5445, "the objects archive holds 5445 model members");
    const Golden objectsGolden[] = {
        { "models/chr/CarmourguardZ/tree", 11878, 0x2bfb68ba },
        { "models/chr/CarmourguardZ/skeleton", 164, 0x21f1cc69 },
        { "models/chr/CarmourguardZ/vtx/000178", 228, 0x19ab2370 },
        { "models/chr/CarmourguardZ/dl/000178/0048e8", 208, 0x40e35cdf },
        { "models/chr/CarmourguardZ/fx/000838", 104, 0x5c292337 },
        { "models/gun/GbriefcaseZ/tree", 362, 0x56b4f8ee },
        { "models/prop/PgoldeneyelogoZ/img/000068", 2836, 0xe4591a99 },
    };
    for (const Golden& g : objectsGolden) {
        auto it = objects.find(g.name);
        Check(it != objects.end() && it->second.size() == g.size && Crc32(it->second) == g.crc, g.name);
    }
    Check(CountPrefix(objects, "models/chr/CarmourguardZ/") == 53, "one model's members");

    auto geometry = ReadZip(dir / "goldeneye_geometry.o2r", ok);
    Check(ok, "geometry archive is a valid zip");
    Check(CountPrefix(geometry, "bg/") == 3474 && CountPrefix(geometry, "obseg/stan/") == 27 &&
              CountPrefix(geometry, "obseg/text/") == 88 && CountPrefix(geometry, "obseg/setup/") == 34 &&
              CountPrefix(geometry, "obseg/brief/") == 20 && CountPrefix(geometry, "font/") == 2,
          "geometry member counts");
    const Golden geometryGolden[] = {
        { "bg/bg_ame/header", 8996, 0xec7b4c6f },
        { "bg/bg_ame/room01/vtx/000000", 1476, 0xd6c7c1de },
        { "bg/bg_ame/room01/dl/primary/000000", 496, 0xe5ae3b3e },
        { "bg/water/1508", 176, 0x73c36f4a },
        { "bg/water/1511", 176, 0x4ee2b6c5 },
        { "obseg/stan/Tbg_ame_all_p_stanZ", 15780, 0x84f27942 },
        { "obseg/stan/Tbg_cat_all_p_stanZ", 35044, 0x3d380eb2 },
        { "obseg/setup/Ump_setupameZ", 17204, 0x777f13db },
        { "obseg/text/LameE", 96, 0xd5342557 },
        { "obseg/brief/UbriefarchZ", 116, 0xf272cacf },
        { "font/fontBankGothic", 9456, 0xeec95cab },
    };
    for (const Golden& g : geometryGolden) {
        auto it = geometry.find(g.name);
        Check(it != geometry.end() && it->second.size() == g.size && Crc32(it->second) == g.crc, g.name);
    }
    fs::remove_all(dir);
}

} // namespace

int main() {
    TestModelTree();
    TestModelTreeErrors();
    TestSkeleton();
    TestBgContainer();
    TestStan();
    TestWaterLists();
    TestStagesOnSyntheticRom();
    TestRealRom();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
