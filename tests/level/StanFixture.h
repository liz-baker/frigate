#pragma once

#include <array>
#include <memory>

#include "level/StanData.h"

namespace StanFixture {

using Frigate::Level::kNoTile;
using Frigate::Level::StanData;
using Frigate::Level::StanTile;

// A triangle whose height plane is its three points in order.
inline StanTile MakeTile(uint32_t id, uint8_t room, const std::array<std::array<int16_t, 3>, 3>& points,
                         const std::array<int32_t, 3>& links) {
    StanTile tile;
    tile.id = id;
    tile.room = room;
    tile.tail = 0x3012;
    for (size_t i = 0; i < 3; i++) {
        tile.points[i].x = points[i][0];
        tile.points[i].y = points[i][1];
        tile.points[i].z = points[i][2];
        tile.points[i].link = links[i];
    }
    return tile;
}

// Two triangles covering x and z in 0..size, linked along the diagonal: tile 0
// (`p5a1`) has the points (0,0), (0,size), (size,size) and tile 1 (`p5a2`)
// (0,0), (size,size), (size,0). The height is base + x * rise / size.
inline std::shared_ptr<StanData> MakeFloor(int16_t size, int16_t base, int16_t rise, uint8_t room) {
    auto data = std::make_shared<StanData>();
    const int16_t high = static_cast<int16_t>(base + rise);
    data->tiles.push_back(MakeTile(0x000501, room, { { { 0, base, 0 }, { 0, base, size }, { size, high, size } } },
                                   { kNoTile, kNoTile, 1 }));
    data->tiles.push_back(MakeTile(0x000502, room, { { { 0, base, 0 }, { size, high, size }, { size, high, 0 } } },
                                   { 0, kNoTile, kNoTile }));
    return data;
}

} // namespace StanFixture
