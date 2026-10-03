#pragma once

#include "game/FrontState.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Game::Menu {

// An entry of the global image table (docs/formats/goldeneye-hud-images.md)
// drawn with its centre at (x, y), tinted and blended by (r, g, b, alpha).
Gfx* DrawTableImage(Gfx* dl, int32_t offset, int32_t width, int32_t height, float x, float y, uint8_t r = 255,
                    uint8_t g = 255, uint8_t b = 255, uint8_t alpha = 255);

// frontDrawCursor: the crosshair at the menu cursor, or the file select's copy
// (`optionIcon` 1) or erase (2) icon in its place.
Gfx* DrawFrontCursor(Gfx* dl, const MenuCursor& cursor, int32_t optionIcon);

} // namespace Frigate::Game::Menu
