#include "game/menu/StageName.h"

#include <span>

#include "game/menu/MissionFolders.h"
#include "game/menu/TitleStrings.h"

namespace Frigate::Game::Menu {

namespace {

constexpr int32_t kDifficultyCount = 4;

constexpr int32_t kLineX = 0x37;
constexpr int32_t kDifficultyY = 0x57;
constexpr int32_t kChapterY = 0x67;
constexpr int32_t kPartY = 0x77;

} // namespace

StageName BuildStageName(const TitleLookup& title, int32_t difficulty, int32_t briefingPage) {
    StageName name;
    const MissionFolderSetup* part = MissionFolderEntryAt(briefingPage);
    if (part == nullptr) {
        return name;
    }

    if (difficulty >= 0) {
        if (difficulty < kDifficultyCount) {
            name.difficulty = title(TitleString::kFirstDifficultyName + static_cast<size_t>(difficulty));
        }
        name.difficulty += title(TitleString::kJamesBond);
    }

    const int32_t chapter = ChapterBriefingEntry(briefingPage);
    if (chapter >= 0) {
        const MissionFolderSetup& heading = MissionFolderEntries()[static_cast<size_t>(chapter)];
        name.chapter = title(TitleString::kMission) + heading.label + ": " +
                       title(static_cast<size_t>(heading.folderTextSlot)) + "\n";
    }

    name.part =
        title(TitleString::kPart) + part->label + ": " + title(static_cast<size_t>(part->folderTextSlot)) + "\n";
    return name;
}

std::vector<TextLine> StageNameLines(const StageName& name) {
    std::vector<TextLine> lines;
    if (!name.difficulty.empty()) {
        lines.push_back({ kLineX, kDifficultyY, name.difficulty });
    }
    if (!name.chapter.empty()) {
        lines.push_back({ kLineX, kChapterY, name.chapter });
    }
    if (!name.part.empty()) {
        lines.push_back({ kLineX, kPartY, name.part });
    }
    return lines;
}

} // namespace Frigate::Game::Menu
