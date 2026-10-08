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

} // namespace Frigate::Level
