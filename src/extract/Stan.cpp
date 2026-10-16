#include "extract/Stan.h"

#include <cstdio>
#include <map>
#include <string>

namespace Frigate::Extract {

namespace {

constexpr size_t kTileHeaderSize = 8;
constexpr size_t kPointSize = 8;
constexpr uint32_t kMinPoints = 3;
constexpr uint32_t kMaxPoints = 10;
constexpr size_t kFirstTilePointerOffset = 4;
// The game addresses a link relative to the first tile minus this.
constexpr int64_t kLinkBaseBias = 0x80;
constexpr uint32_t kNoLink = 0xFFFF;

Error StanError(const std::string& what) {
    return Error::Make(ErrorCode::Corrupt, "A stage's collision file does not have the expected layout: " + what + ".");
}

std::string Hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

uint32_t Be32(std::span<const uint8_t> d, size_t at) {
    return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | uint32_t(d[at + 3]);
}

uint32_t Be16(std::span<const uint8_t> d, size_t at) {
    return (uint32_t(d[at]) << 8) | d[at + 1];
}

void Put16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    Put16(out, v >> 16);
    Put16(out, v);
}

} // namespace

Error ConvertStanFile(std::span<const uint8_t> data, std::vector<uint8_t>& payload) {
    if (data.size() < kFirstTilePointerOffset + 4) {
        return StanError("it is shorter than its header");
    }
    uint32_t first = Be32(data, kFirstTilePointerOffset);

    struct RawTile {
        size_t at;
        uint32_t count;
    };
    std::vector<RawTile> tiles;
    std::map<size_t, size_t> indexAt;
    size_t cursor = first;
    for (;;) {
        if (cursor + 4 > data.size()) {
            return StanError("the tile list runs off the end of the file at " + Hex(cursor));
        }
        // The list ends at a tile whose id and room are all zero.
        if (Be32(data, cursor) == 0) {
            break;
        }
        if (cursor + kTileHeaderSize > data.size()) {
            return StanError("the tile at " + Hex(cursor) + " is cut off");
        }
        uint32_t count = Be16(data, cursor + 6) >> 12;
        if (count < kMinPoints || count > kMaxPoints) {
            return StanError("the tile at " + Hex(cursor) + " has " + std::to_string(count) + " points");
        }
        size_t size = kTileHeaderSize + kPointSize * count;
        if (cursor + size > data.size()) {
            return StanError("the tile at " + Hex(cursor) + " is cut off");
        }
        indexAt[cursor] = tiles.size();
        tiles.push_back({ cursor, count });
        cursor += size;
    }
    if (tiles.size() >= kNoLink) {
        return StanError(std::to_string(tiles.size()) + " tiles do not fit a 16-bit link");
    }

    int64_t base = int64_t(first) - kLinkBaseBias;
    payload.clear();
    Put32(payload, uint32_t(tiles.size()));
    for (const RawTile& tile : tiles) {
        payload.insert(payload.end(), data.begin() + std::ptrdiff_t(tile.at),
                       data.begin() + std::ptrdiff_t(tile.at + kTileHeaderSize));
        for (uint32_t i = 0; i < tile.count; i++) {
            size_t at = tile.at + kTileHeaderSize + kPointSize * i;
            payload.insert(payload.end(), data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + 6));
            uint32_t link = Be16(data, at + 6);
            uint32_t target = kNoLink;
            if ((link >> 4) != 0) {
                int64_t address = base + (int64_t(link) << 3);
                auto it = address >= 0 ? indexAt.find(size_t(address)) : indexAt.end();
                if (it == indexAt.end()) {
                    return StanError("the tile at " + Hex(tile.at) + " links to " + Hex(uint64_t(address)) +
                                     ", which is not a tile");
                }
                target = uint32_t(it->second);
            }
            Put16(payload, target);
        }
    }
    return {};
}

} // namespace Frigate::Extract
