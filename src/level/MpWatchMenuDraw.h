#pragma once

#include "level/BgView.h"
#include "level/MpWatchMenu.h"
#include "libultraship/libultra/gbi.h"
#include "resource/type/Font.h"

namespace Frigate::Level {

// The lines of `plan` drawn in `font`, with the RDP state the text assumes set
// up before and put back after. `screen` carries viGetX and viGetY, and `view`
// is the player's view the prompt's scissor is set to.
Gfx* DrawMpWatchPlan(Gfx* dl, const MpWatchPlan& plan, const Font& font, const ViewMetrics& screen,
                     const PlayerView& view);

// The dimming over a player's view that an open menu draws
// (microcode_constructor_related_to_menus over the view). A display wider than
// 4:3 (`widen` != 1) fills the whole width of the view's columns, which the 4:3
// rectangle of 2D would leave short of.
Gfx* DrawMpMenuDim(Gfx* dl, const ViewMetrics& screen, float widen);

} // namespace Frigate::Level
