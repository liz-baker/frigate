#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/menu/BriefingModel.h"
#include "game/menu/PageText.h"
#include "level/Objectives.h"

namespace Frigate::Game::Menu {

// print_objectives_and_status_to_menu from row `top`: each objective that
// applies at `difficulty`, lettered, with its text wrapped beside it. With
// `statuses` (the decomp's arg3), the text wraps narrower and each objective's
// verdict, Completed or Failed, is listed at the right.
std::vector<TextLine> ObjectiveLines(const PageText& text,
                                     const std::array<BriefingData::Objective, kObjectivesMax>& objectives,
                                     int32_t difficulty, int32_t top,
                                     const std::array<Level::ObjectiveStatus, kObjectivesMax>* statuses);

} // namespace Frigate::Game::Menu
