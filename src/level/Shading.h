#pragma once

#include <array>
#include <cstdint>

#include "level/StanData.h"

namespace Frigate::Level {

// An rgba_u8: the colour a prop is shaded with, and the alpha of that shading.
using ShadeColour = std::array<uint8_t, 4>;

// copy_tile_RGB_as_24bit: the tile's floor colour, each of its three nibbles
// doubled up into a byte.
std::array<uint8_t, 3> TileRgb(const StanTile& tile);

// set_color_shading_from_tile: the shading a prop standing on `tile` takes.
// The floor colour is reduced to its hue at full saturation, halved, and given
// an alpha from how dark the floor is.
ShadeColour ShadeFromTile(const StanTile& tile);

// sub_GAME_7F0402B4, after its check for an illuminated object: the shading
// an object or door standing on `tile` aims for, which is halved once more.
ShadeColour ObjectShadeFromTile(const StanTile& tile);

// update_color_shading: `dest` an eighth of the way (rounding up) to `src`.
void EaseShading(ShadeColour& dest, const ShadeColour& src);

} // namespace Frigate::Level
