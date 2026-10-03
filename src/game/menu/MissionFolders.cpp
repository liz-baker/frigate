#include "game/menu/MissionFolders.h"

namespace Frigate::Game::Menu {

namespace {

using Level::LevelId;

constexpr MissionFolderSetup kEntries[] = {
    { "1", 120, 0, kNoLevel, 0, MissionFolderType::Header, -1, nullptr },
    { "i", 121, 0, LevelId::Dam, 0, MissionFolderType::Part, 0, "UbriefdamZ" },
    { "ii", 122, 0, LevelId::Facility, 0, MissionFolderType::Part, 1, "UbriefarkZ" },
    { "iii", 123, 0, LevelId::Runway, 0, MissionFolderType::Part, 2, "UbriefrunZ" },
    { "2", 124, 0, kNoLevel, 0, MissionFolderType::Header, -1, nullptr },
    { "i", 125, 0, LevelId::Surface, 0, MissionFolderType::Part, 3, "UbriefsevxZ" },
    { "ii", 126, 0, LevelId::Bunker1, 0, MissionFolderType::Part, 4, "UbriefsevbunkerZ" },
    { "3", 127, 0, kNoLevel, 1, MissionFolderType::Header, -1, nullptr },
    { "i", 128, 129, LevelId::Silo, 1, MissionFolderType::Part, 5, "UbriefsiloZ" },
    { "4", 130, 0, kNoLevel, 1, MissionFolderType::Header, -1, nullptr },
    { "i", 131, 0, LevelId::Frigate, 1, MissionFolderType::Part, 6, "UbriefdestZ" },
    { "5", 124, 0, kNoLevel, 1, MissionFolderType::Header, -1, nullptr },
    { "i", 125, 0, LevelId::Surface2, 1, MissionFolderType::Part, 7, "UbriefsevxbZ" },
    { "ii", 126, 0, LevelId::Bunker2, 1, MissionFolderType::Part, 8, "UbriefsevbZ" },
    { "6", 132, 0, kNoLevel, 2, MissionFolderType::Header, -1, nullptr },
    { "i", 133, 134, LevelId::Statue, 2, MissionFolderType::Part, 9, "UbriefstatueZ" },
    { "ii", 135, 136, LevelId::Archives, 2, MissionFolderType::Part, 10, "UbriefarchZ" },
    { "iii", 137, 0, LevelId::Streets, 2, MissionFolderType::Part, 11, "UbriefpeteZ" },
    { "iv", 138, 0, LevelId::Depot, 2, MissionFolderType::Part, 12, "UbriefdepoZ" },
    { "v", 139, 0, LevelId::Train, 2, MissionFolderType::Part, 13, "UbrieftraZ" },
    { "7", 140, 0, kNoLevel, 3, MissionFolderType::Header, -1, nullptr },
    { "i", 141, 0, LevelId::Jungle, 3, MissionFolderType::Part, 14, "UbriefjunZ" },
    { "ii", 142, 143, LevelId::Control, 3, MissionFolderType::Part, 15, "UbriefcontrolZ" },
    { "iii", 144, 145, LevelId::Caverns, 3, MissionFolderType::Part, 16, "UbriefcaveZ" },
    { "iv", 146, 147, LevelId::Cradle, 3, MissionFolderType::Part, 17, "UbriefcradZ" },
    { "8", 148, 0, kNoLevel, 4, MissionFolderType::Header, -1, nullptr },
    { "i", 149, 150, LevelId::Aztec, 4, MissionFolderType::Part, 18, "UbriefaztZ" },
    { "9", 151, 0, kNoLevel, 4, MissionFolderType::Header, -1, nullptr },
    { "i", 152, 153, LevelId::Egypt, 4, MissionFolderType::Part, 19, "UbriefcrypZ" },
    { nullptr, 0, 0, kNoLevel, -1, MissionFolderType::Part, -1, nullptr },
};

} // namespace

std::span<const MissionFolderSetup> MissionFolderEntries() {
    return kEntries;
}

int32_t ChapterBriefingEntry(int32_t briefing) {
    for (int32_t i = briefing; i >= 0; i--) {
        if (kEntries[i].type != MissionFolderType::Part) {
            return i;
        }
    }
    return -1;
}

} // namespace Frigate::Game::Menu
