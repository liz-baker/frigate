#pragma once

#include "game/FrontState.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Game::Menu {

// frontDrawCursor: the crosshair at the menu cursor, or the file select's copy
// (`optionIcon` 1) or erase (2) icon in its place.
Gfx* DrawFrontCursor(Gfx* dl, const MenuCursor& cursor, int32_t optionIcon);

} // namespace Frigate::Game::Menu
