#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/menu/PageText.h"

namespace Frigate::Game::Menu {

// The lines print_current_solo_briefing_stage_name draws at the top of the
// difficulty, 007 options and briefing pages; a line is empty when it is not
// shown.
struct StageName {
    // The difficulty with its " JB" suffix.
    std::string difficulty;
    // "Mission <n>: <chapter name>".
    std::string chapter;
    // "Part <roman numeral>: <stage name>".
    std::string part;
};

// The LTITLE string in a slot, empty for a slot the bank does not have.
using TitleLookup = std::function<std::string(size_t slot)>;

// print_current_solo_briefing_stage_name for the mission folder row
// `briefingPage` at `difficulty` (-1 in a multiplayer game, which shows no
// difficulty). Every line is empty for a row that is not a stage.
StageName BuildStageName(const TitleLookup& title, int32_t difficulty, int32_t briefingPage);

// Where the name's lines print: the lines of `name` that are shown, in black.
std::vector<TextLine> StageNameLines(const StageName& name);

} // namespace Frigate::Game::Menu
