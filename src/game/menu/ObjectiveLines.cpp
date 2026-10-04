#include "game/menu/ObjectiveLines.h"

#include <algorithm>

namespace Frigate::Game::Menu {

namespace {

constexpr int32_t kLetterX = 0x37;
constexpr int32_t kTextX = 0x4B;
constexpr int32_t kStatusX = 0x136;
// The wrap width without, and with, the verdicts listed.
constexpr int32_t kWrapWidth = 0x140;
constexpr int32_t kWrapWidthWithStatus = 0xDC;
// setTextWordWrap(2) around the narrower wrap.
constexpr int32_t kIndentWithStatus = 2;

// TITLE_STR_91_COMPLETED and TITLE_STR_92_FAILED.
constexpr int32_t kStringCompleted = 91;
constexpr int32_t kStringFailed = 92;

int32_t LineCount(const std::string& text) {
    return static_cast<int32_t>(std::count(text.begin(), text.end(), '\n'));
}

} // namespace

std::vector<TextLine> ObjectiveLines(const PageText& text,
                                     const std::array<BriefingData::Objective, kObjectivesMax>& objectives,
                                     int32_t difficulty, int32_t top,
                                     const std::array<Level::ObjectiveStatus, kObjectivesMax>* statuses) {
    std::vector<TextLine> lines;
    int32_t shown = 0;
    int32_t rows = 0;
    for (size_t i = 0; i < kObjectivesMax; i++) {
        const BriefingData::Objective& objective = objectives[i];
        if (objective.textId == 0 || difficulty < objective.enabledDifficulty) {
            continue;
        }
        std::string letter = "a.\n";
        letter[0] = static_cast<char>(letter[0] + shown);
        const int32_t y = (text.Measure(letter).height * rows) + top;
        lines.push_back({ kLetterX, y, letter, PageColour::kBlack });

        const std::string wrapped = statuses != nullptr
                                        ? text.Wrap(kWrapWidthWithStatus, text.Get(objective.textId), kIndentWithStatus)
                                        : text.Wrap(kWrapWidth, text.Get(objective.textId), 0);
        lines.push_back({ kTextX, y, wrapped, PageColour::kBlack });

        if (statuses != nullptr) {
            if ((*statuses)[i] == Level::ObjectiveStatus::Complete) {
                lines.push_back({ kStatusX, y, text.Get(TitleId(kStringCompleted)), PageColour::kBlack });
            } else {
                lines.push_back({ kStatusX, y, text.Get(TitleId(kStringFailed)), PageColour::kFailedRed });
            }
        }

        rows += LineCount(wrapped);
        shown++;
    }
    return lines;
}

} // namespace Frigate::Game::Menu
