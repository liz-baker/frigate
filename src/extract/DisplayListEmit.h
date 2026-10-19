#pragma once

#include "extract/Error.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Frigate::Extract {

// Walks GoldenEye's native display lists and re-emits them as libultraship Fast::DisplayList resources whose
// vertex, child-list and texture commands name other resources by path. Resource paths and command encodings:
// docs/formats/goldeneye-display-list-resource.md; the opcodes: docs/formats/goldeneye-display-list.md.

constexpr uint32_t kG_MTX = 0x01;
constexpr uint32_t kG_MOVEMEM = 0x03;
constexpr uint32_t kG_VTX = 0x04;
constexpr uint32_t kG_DL = 0x06;
constexpr uint32_t kG_ENDDL = 0xB8;
constexpr uint32_t kG_TRI4 = 0xB1;
constexpr uint32_t kG_SETTIMG = 0xFD;
constexpr uint32_t kG_LOADBLOCK = 0xF3;
constexpr uint32_t kG_SETTILESIZE = 0xF2;
constexpr uint32_t kG_SETTILE = 0xF5;
constexpr uint32_t kG_SETTEX = 0xC0;
constexpr uint32_t kG_DL_NOPUSH = 0x01; // in a G_DL's flag byte: jump instead of call

// libultraship's opcodes for a command carrying a 64-bit path hash.
constexpr uint32_t kOtrG_SETTIMG_HASH = 0x20;
constexpr uint32_t kOtrG_DL_HASH = 0x31;
constexpr uint32_t kOtrG_VTX_HASH = 0x32;

constexpr size_t kVertexSize = 16;
constexpr size_t kGfxSize = 8;

// Fast::DisplayList reads this as its first payload byte (ucode_indy_ge), then skips to the next 8-byte boundary
// of the file.
constexpr uint8_t kUcodeIndyGe = 6;

// G_SETTEX request fields: the type, the sample offset and the wrap-mode fields of a tile request.
constexpr uint32_t kRequestTypeMipmap = 2;
constexpr uint32_t kRequestTypeTilePreswapped = 4;
constexpr uint32_t kRequestSampleOffset = 2;

constexpr const char* kDisplayListResourceType = "GEDL";
constexpr uint32_t kDisplayListResourceVersion = 0;
constexpr const char* kVertexResourceType = "OVTX";
constexpr uint32_t kVertexResourceVersion = 0;

// libultraship's CRC64(const char*): CRC-64/ECMA-182 (MSB first), all-ones seed, no final inversion.
uint64_t PathHash(std::string_view path);

// A 16-byte command: the opcode and flags, `w1`, then the target path's hash.
void AppendReferenceCommand(std::vector<uint8_t>& out, uint32_t opcode, uint32_t flags, std::string_view path,
                            uint32_t w1 = 0);
void AppendWord(std::vector<uint8_t>& out, uint32_t value);

// A texture table entry's wrap flags (G_TX_*) as a request's mode field: 0 wrap, 1 clamp, 2 mirror.
uint32_t RequestWrapMode(uint32_t tileFlags);

// Where a G_SETTEX texture id lives: its texture resource path and the N64 format, size and width G_SETTIMG must
// carry to read it. A paletted image also names its companion palette resource.
struct TextureBinding {
    std::string path;
    uint32_t fmt = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    std::string palettePath;
    uint16_t detail = 0;
};

using TextureLookup = std::function<Error(uint32_t textureId, TextureBinding& binding)>;

// The list a texSelect() single-tile request builds: the request, the texture reference and the end of the list.
std::vector<uint8_t> EncodeTextureRequestList(uint32_t request, uint32_t textureId, const TextureBinding& binding);

// Maps a segment address to an offset in the buffer being walked, or nullopt.
using Resolve = std::function<std::optional<uint32_t>(uint32_t address)>;

struct VertexRef {
    uint32_t cmdOffset;
    uint32_t offset; // resolved offset of the first vertex in the buffer
    uint32_t count;
    uint32_t destIndex; // first vertex-buffer slot the load fills
};

struct TextureRef {
    uint32_t cmdOffset;
    uint32_t offset;
    uint32_t fmt;
    uint32_t size; // N64 pixel size code (0=4b, 1=8b, 2=16b, 3=32b)
};

struct AddrRef {
    uint32_t cmdOffset;
    uint32_t offset;
};

struct DisplayListRefs {
    std::vector<VertexRef> vertices;
    std::vector<TextureRef> textures;
    std::vector<AddrRef> matrices;
    std::vector<AddrRef> movemem;
    std::vector<AddrRef> childLists;
    std::set<uint32_t> visitedLists;
    // Where the walker could not follow: an address that does not resolve, a list that runs off the buffer, or the
    // command cap.
    std::vector<int64_t> unresolved;
    size_t commandCount = 0;
};

// Walks the list at `start` and every list it calls or jumps to, collecting each once. `maxCommands` bounds the
// walk over corrupt data that never reaches a terminator.
DisplayListRefs WalkDisplayList(std::span<const uint8_t> data, uint32_t start, const Resolve& resolve,
                                size_t maxCommands = 1000000);

// Collapses vertex loads into disjoint (offset, byte length) ranges, merging overlapping or touching loads.
std::vector<std::pair<uint32_t, uint32_t>> MergeVertexRanges(const DisplayListRefs& refs);

// Pixel data a list's G_SETTIMG points at inside its buffer, exactly the texels its G_LOADBLOCK loads.
struct EmbeddedImage {
    std::string path;
    uint32_t offset = 0;
    uint32_t fmt = 0;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t texels = 0;
    std::vector<uint8_t> data;
};

struct EmittedList {
    std::string path;
    std::vector<uint8_t> payload;
};

struct ReemittedLists {
    std::vector<EmittedList> lists; // by list offset
    std::vector<EmbeddedImage> images;

    EmbeddedImage* FindImage(std::string_view path);
    // Replaces an image at the same path, keeping its place, or appends it.
    void SetImage(EmbeddedImage image);
};

using NameFn = std::function<std::string(uint32_t offset)>;
// Where a G_VTX goes: the vertex resource's path and the byte offset into it.
using VertexTargetFn = std::function<Error(const VertexRef& ref, std::string& path, uint32_t& byteOffset)>;

// One buffer's lists to re-emit: where they start, how addresses resolve, and what each list, embedded image and
// vertex load is called.
struct ReemitSpec {
    std::string what; // for error messages
    std::span<const uint8_t> data;
    std::vector<uint32_t> roots;
    Resolve resolve;
    NameFn listPath;  // list offset -> resource path
    NameFn imagePath; // embedded image offset -> resource path
    VertexTargetFn vertexTarget;
    const TextureLookup* textures = nullptr;
};

// Appends to `out` every list reachable from the spec's roots, and the images they load from the buffer.
Error ReemitLists(const ReemitSpec& spec, ReemittedLists& out);

// The lists reachable from `roots` in a plain data segment (lists, vertices and images laid out in one buffer with
// no container), as `<basePath>/dl/<offset>` resources referencing `<basePath>/vtx/<offset>` and
// `<basePath>/img/<offset>`. `vertexRanges` are the (offset, byte length) spans the caller writes as vertex
// resources, named by their start offset; every G_VTX must fall inside one.
Error ReemitSegmentLists(const std::string& basePath, std::span<const uint8_t> data, const std::vector<uint32_t>& roots,
                         const Resolve& resolve, const std::vector<std::pair<uint32_t, uint32_t>>& vertexRanges,
                         const TextureLookup& textures, ReemittedLists& out);

// Fast::Texture (V1) payload after the header: type, width, height, flags, the two scales, then the data. Layout:
// docs/formats/o2r-resource-header.md.
std::vector<uint8_t> EncodeTexturePayload(uint32_t textureType, uint32_t width, uint32_t height,
                                          std::span<const uint8_t> pixels);
Error EncodeEmbeddedImagePayload(const EmbeddedImage& image, std::vector<uint8_t>& payload);

// Fast::Vertex payload after the header: a big-endian count, then the vertex records as they are in the ROM.
// Layout: docs/formats/goldeneye-vertex.md.
Error EncodeVertexPayload(std::span<const uint8_t> vertices, std::vector<uint8_t>& payload);

} // namespace Frigate::Extract
