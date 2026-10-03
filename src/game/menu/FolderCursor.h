#pragma once

#include "game/FrontState.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Game::Menu {

// frontDrawCursor with the crosshair (folder_selection_screen_option_icon 0),
// the cursor of every folder screen after the file select: the image at the
// rounded cursor position, slightly translucent.
Gfx* DrawCrosshairCursor(Gfx* dl, const MenuCursor& cursor);

} // namespace Frigate::Game::Menu
