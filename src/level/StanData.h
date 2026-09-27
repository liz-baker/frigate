#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frigate::Level {

// The most points a tile has (list_of_tilesizes ends at 10).
constexpr int kMaxTilePoints = 10;
// A point's link when no tile lies across its edge.
constexpr int32_t kNoTile = -1;

// StandTilePoint. `link` is the index of the tile across the edge that starts
// at this point, or kNoTile.
struct StanPoint {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int32_t link = kNoTile;
};

// StandTile. Coordinates are in stan units (world units times the stage scale).
// Layout: docs/formats/goldeneye-stan-resource.md.
struct StanTile {
    // The 24-bit name.
    uint32_t id = 0;
    uint8_t room = 0;
    uint16_t mid = 0;
    uint16_t tail = 0;
    std::array<StanPoint, kMaxTilePoints> points{};

    // getsides(): the tail's high nibble.
    int PointCount() const {
        return (tail >> 12) & 0xF;
    }
    // The point index the tail names for the tile's k'th height-plane vertex
    // (k = 0..2).
    int Triple(int k) const {
        return (tail >> (8 - 4 * k)) & 0xF;
    }
    // The tile's special kind, an index into g_StanTileSpecialFlags.
    int Special() const {
        return mid >> 12;
    }
};

// A stage's stan file, tiles in file order (so a room's tiles are contiguous).
struct StanData {
    std::vector<StanTile> tiles;
};

// Reads a `GEST` payload (after the resource header). False when it is
// truncated, has more tiles than it holds, or links to a tile that does not
// exist.
bool ParseStanPayload(const uint8_t* data, size_t size, StanData* out);

} // namespace Frigate::Level
