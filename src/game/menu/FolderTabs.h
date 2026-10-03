#pragma once

#include <string_view>

#include "libultraship/libultra/gbi.h"
#include "resource/type/Font.h"

namespace Frigate::Game::Menu {

// frontAddPreviousTabText: the Previous tab's label, turned sideways in the
// small gothic font, with a dark box behind it while `highlighted`
// (tab_prev_highlight). `label` is LTITLE string 6.
Gfx* DrawPreviousTab(Gfx* dl, const Font& gothic, std::string_view label, bool highlighted);

} // namespace Frigate::Game::Menu
