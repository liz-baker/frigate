#pragma once

#include <cstdint>

#include "fast/resource/type/Texture.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Game::Menu {

// FolderSelectColour.
struct FolderColour {
    int32_t red;
    int32_t green;
    int32_t blue;
};

// titleRenderFolderMenuBackground and titleRenderFolderMenuBackgroundLines:
// the gun barrel backdrop (`eyeintro/sight`, 440x299 8-bit intensity), drawn
// a row at a time and tinted from `top` to `bottom` down the screen, scrolled
// by `xOffset`. The caller sets the combiner.
Gfx* DrawFolderBackground(Gfx* dl, const Fast::Texture& backdrop, int32_t xOffset, const FolderColour& top,
                          const FolderColour& bottom);

} // namespace Frigate::Game::Menu
