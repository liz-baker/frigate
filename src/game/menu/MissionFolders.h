#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "level/LevelInfo.h"

namespace Frigate::Game::Menu {

// MISSION_HEADER and MISSION_PART: a chapter's heading row, or one of its
// stages.
enum class MissionFolderType : int32_t { Header, Part };

// LEVELID_NONE.
constexpr Level::LevelId kNoLevel = static_cast<Level::LevelId>(-1);

// mission_folder_setup: one row of the table the folder, mission select and
// briefing screens list chapters and stages from.
struct MissionFolderSetup {
    // The chapter number or the stage's roman numeral; null ends the table.
    const char* label;
    // LTITLE string slots: the stage's name and, for a long name, the short one
    // the mission select's icons show (0 for none).
    int32_t folderTextSlot;
    int32_t iconTextSlot;
    Level::LevelId stage;
    int32_t unknown;
    MissionFolderType type;
    // The LEVEL_SOLO_SEQUENCE value of a part; -1 for a heading.
    int32_t missionNum;
    // The briefing's model name (`obseg/prop`-style `U...Z`), null for a heading.
    const char* briefing;
};

// mission_folder_setup_entries, including its terminating row.
std::span<const MissionFolderSetup> MissionFolderEntries();

// The row a briefingpage names, or null for a page outside the table.
const MissionFolderSetup* MissionFolderEntryAt(int32_t page);

// get_chapter_briefing_entry: the index of the nearest heading at or before
// row `briefing`, or -1.
int32_t ChapterBriefingEntry(int32_t briefing);

} // namespace Frigate::Game::Menu
