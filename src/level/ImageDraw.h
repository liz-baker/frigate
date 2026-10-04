#pragma once

#include <cstdint>

#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// texSelect for a plain (level 0) image of the global image table
// (docs/formats/goldeneye-hud-images.md): the render mode for `mode` (2 to 4 as
// the decomp's callers pass), the texture on, and the entry's load, written by
// the packer as the list `hud/img/<offset>`.
Gfx* SelectImage(Gfx* dl, int32_t offset, int32_t mode);

// draw_textured_rectangle: the rectangle centred on `position`, `size` being
// its half extents.
Gfx* DrawTexturedRectangle(Gfx* dl, const float position[2], const float size[2], int32_t width, int32_t height,
                           bool flipXY, bool flipX, bool flipY);

// display_image_at_position for a plain image with no highlight: the
// environment colour tints it, and its alpha is the environment's.
Gfx* DisplayImageAtPosition(Gfx* dl, const float position[2], const float size[2], int32_t width, int32_t height,
                            bool flipXY, bool flipX, bool flipY, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);

} // namespace Frigate::Level
