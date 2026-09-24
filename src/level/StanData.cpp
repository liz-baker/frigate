#include "level/StanData.h"

namespace Frigate::Level {

namespace {

constexpr size_t kTileHeaderSize = 8;
constexpr size_t kPointSize = 8;
constexpr uint16_t kNoLinkValue = 0xFFFF;

uint32_t ReadU32(const uint8_t* at) {
    return (uint32_t{ at[0] } << 24) | (uint32_t{ at[1] } << 16) | (uint32_t{ at[2] } << 8) | uint32_t{ at[3] };
}

uint16_t ReadU16(const uint8_t* at) {
    return static_cast<uint16_t>((at[0] << 8) | at[1]);
}

} // namespace

bool ParseStanPayload(const uint8_t* data, size_t size, StanData* out) {
    if (size < 4) {
        return false;
    }
    const uint32_t count = ReadU32(data);
    // Every tile takes at least a header and three points.
    if (count > (size - 4) / (kTileHeaderSize + 3 * kPointSize)) {
        return false;
    }

    out->tiles.assign(count, StanTile{});
    size_t cursor = 4;
    for (StanTile& tile : out->tiles) {
        if (size - cursor < kTileHeaderSize) {
            return false;
        }
        const uint32_t idAndRoom = ReadU32(data + cursor);
        tile.id = idAndRoom >> 8;
        tile.room = static_cast<uint8_t>(idAndRoom);
        tile.mid = ReadU16(data + cursor + 4);
        tile.tail = ReadU16(data + cursor + 6);
        cursor += kTileHeaderSize;

        const int pointCount = tile.PointCount();
        if (pointCount < 3 || pointCount > kMaxTilePoints || size - cursor < pointCount * kPointSize) {
            return false;
        }
        for (int i = 0; i < pointCount; i++) {
            StanPoint& point = tile.points[i];
            point.x = static_cast<int16_t>(ReadU16(data + cursor));
            point.y = static_cast<int16_t>(ReadU16(data + cursor + 2));
            point.z = static_cast<int16_t>(ReadU16(data + cursor + 4));
            const uint16_t link = ReadU16(data + cursor + 6);
            if (link == kNoLinkValue) {
                point.link = kNoTile;
            } else if (link >= count) {
                return false;
            } else {
                point.link = link;
            }
            cursor += kPointSize;
        }
    }
    return true;
}

} // namespace Frigate::Level
