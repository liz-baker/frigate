#include "game/menu/StageName.h"

#include <span>

#include "game/menu/MissionFolders.h"
#include "game/menu/TitleStrings.h"

namespace Frigate::Game::Menu {

namespace {

constexpr int32_t kDifficultyCount = 4;

} // namespace

StageName BuildStageName(const TitleLookup& title, int32_t difficulty, int32_t briefingPage) {
    const std::span<const MissionFolderSetup> entries = MissionFolderEntries();
    StageName name;
    if (briefingPage < 0 || static_cast<size_t>(briefingPage) >= entries.size()) {
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
        const MissionFolderSetup& heading = entries[static_cast<size_t>(chapter)];
        name.chapter = title(TitleString::kMission) + heading.label + ": " +
                       title(static_cast<size_t>(heading.folderTextSlot)) + "\n";
    }

    const MissionFolderSetup& part = entries[static_cast<size_t>(briefingPage)];
    name.part = title(TitleString::kPart) + part.label + ": " + title(static_cast<size_t>(part.folderTextSlot)) + "\n";
    return name;
}

} // namespace Frigate::Game::Menu
