#pragma once

#include <cstddef>

#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

class FolderPaper;

// The player panels the multiplayer handicap, control style and team screens
// share: a 2 by 2 grid (two wide rows with two players) divided by two rules,
// each player's panel showing a heading until they lock in and their current
// choice below it.

// The two rules (a horizontal one, and a vertical one with three or more
// players) between the panels.
Gfx* DrawPanelRules(Gfx* dl, int players);

// The panel of the handicap and control style screens: `player`'s heading
// (only while `locked` is false) over `valueSlot`'s text, centred in a panel
// whose size depends on the player count.
Gfx* DrawChoicePanel(Gfx* dl, const FolderPaper& paper, int player, int players, bool locked, size_t headingSlot,
                     size_t valueSlot);

} // namespace Frigate::Game::Menu
