#pragma once

#include "extract/DisplayListEmit.h"
#include "extract/Error.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Frigate::Extract {

// The model and bg containers, unwrapped into plain buffers the display-list walker can read. On-disk layout:
// docs/formats/goldeneye-containers.md.

constexpr uint32_t kModelSegmentBase = 0x05000000;
constexpr uint32_t kModelVertexSegment = 0x04;
constexpr uint32_t kModelMatrixSegment = 0x03;
constexpr uint32_t kBgRoomVertexSegment = 0x0E;
constexpr uint32_t kBgFileSegment = 0x0F;
constexpr uint32_t kBgRoomEntrySize = 24;

// A decompressed model file. Its node, display-list and vertex pointers are segment-5 addresses over `data`.
struct ModelImage {
    std::vector<uint8_t> data;

    // Segment address to buffer offset; nullopt for null or an address outside this file.
    std::optional<uint32_t> Resolve(uint32_t address) const;
    // Resolver for one display-list node's lists. The game points segment 4 at the node's own vertex array before
    // drawing it, so G_VTX addresses in a node's lists are relative to that array. Segment 3 holds matrices the game
    // builds at run time, so its addresses resolve to their bare segment offset.
    Frigate::Extract::Resolve DisplayListResolver(uint32_t verticesAddress) const;
};

// One bg room's decompressed blobs. `vertices` is what its display lists' G_VTX addresses index into, and is empty
// for a room with no geometry; `hasSecondary` is false when the room has no secondary list.
struct BgRoom {
    uint32_t index = 0;
    std::vector<uint8_t> vertices;
    std::vector<uint8_t> primary;
    std::vector<uint8_t> secondary;
    bool hasSecondary = false;

    // Offset into `vertices` for a segment-0x0e address; nullopt for anything else or an address past the blob.
    std::optional<uint32_t> Resolve(uint32_t address) const;
};

// A bg file's room table, validated: every room's blob pointers as file offsets (nullopt when absent).
struct BgFile {
    struct Room {
        std::optional<uint32_t> vertices;
        uint32_t primary = 0;
        std::optional<uint32_t> secondary;
    };
    std::span<const uint8_t> data;
    std::vector<Room> rooms; // in index order from 1
    // The bytes the game reads before any room: up to the first room's vertex blob, rounded up to 16.
    std::span<const uint8_t> header;

    // Fails with Corrupt for a file with no room table or no rooms to size the header by (an empty placeholder).
    static Error Parse(std::span<const uint8_t> data, BgFile& out);
    // Decompresses room `n` (zero-based into `rooms`; its index is n + 1).
    Error InflateRoom(size_t n, BgRoom& room) const;

  private:
    Error InflateBlob(uint32_t offset, std::vector<uint8_t>& out) const;
    std::vector<uint32_t> boundaries; // every blob's file offset, ascending
};

// Inflates a model file (the 1172 container).
Error UnwrapModelFile(std::span<const uint8_t> compressed, ModelImage& image);

} // namespace Frigate::Extract
