#include "game/menu/MissionSelectModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/MissionFolders.h"
#include "game/save/SaveData.h"

namespace Frigate::Game::Menu {

namespace {

using Input::Button::kA;
using Input::Button::kB;
using Input::Button::kStart;
using Input::Button::kZ;

constexpr uint16_t kSelectButtons = kA | kStart | kZ;

// The level number of the icon at a row and column.
int32_t LevelAt(int32_t row, int32_t column) {
    return (row * MissionSelectModel::kColumns) + column;
}

} // namespace

void MissionSelectModel::Init() {
    selectedStage_ = static_cast<int32_t>(kNoLevel);
    briefingPage_ = -1;
    tabNextSelected_ = false;
    tabPrevSelected_ = false;
}

int32_t MissionSelectModel::EntryForLevel(int32_t level) {
    const std::span<const MissionFolderSetup> entries = MissionFolderEntries();
    for (size_t i = 0; entries[i].folderTextSlot != 0; i++) {
        if (level == entries[i].missionNum) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t MissionSelectModel::HighestUnlockedDifficulty(int32_t level, const MissionSelectProgress& progress) {
    // The decomp indexes the table with the lookup's result unchecked; every
    // icon the screen asks about is a mission, so a miss only guards the read.
    const int32_t entry = EntryForLevel(level);
    if (entry < 0 || MissionFolderEntries()[static_cast<size_t>(entry)].stage < static_cast<Level::LevelId>(0)) {
        return -1;
    }

    int32_t highest = Save::kDifficulty00;
    if (progress.Is007ModeUnlocked()) {
        highest = Save::kDifficulty007;
    }
    for (int32_t difficulty = highest; difficulty >= 0; difficulty--) {
        const Save::StageStatus status = progress.StatusAt(level, difficulty);
        if (!progress.CheatVariant()) {
            if (status != Save::StageStatus::Locked) {
                return difficulty;
            }
        } else if (status == Save::StageStatus::Completed) {
            return difficulty;
        }
    }
    return -1;
}

IconColour MissionSelectModel::IconVertexColour(int32_t level, int32_t highlighted,
                                                const MissionSelectProgress& progress) {
    if (HighestUnlockedDifficulty(level, progress) >= 0) {
        if (level == highlighted) {
            return { 0xff, 0xff, 0xff, 0xf5 };
        }
        return { 0x6e, 0x6e, 0x6e, 0xff };
    }
    return { 0xf, 0xf, 0xf, 0xff };
}

void MissionSelectModel::PlaceCursorOnStage(int32_t level, MenuCursor& cursor) {
    cursor.h = static_cast<float>(kColumnX[static_cast<size_t>(level % kColumns)]);
    cursor.v = static_cast<float>(kRowY[static_cast<size_t>(level / kColumns)]);
}

int32_t MissionSelectModel::FindHighlighted(const MenuCursor& cursor, const MissionSelectProgress& progress) const {
    auto unlocked = [&progress](int32_t level) { return HighestUnlockedDifficulty(level, progress) >= 0; };

    // The nearest column and row to the cursor: the first whose midpoint to the
    // next lies past it.
    int32_t column = 0;
    while (column < kColumns - 1) {
        const float midpoint =
            static_cast<float>(kColumnX[static_cast<size_t>(column)] + kColumnX[static_cast<size_t>(column) + 1]) *
            0.5f;
        if (midpoint > cursor.h) {
            break;
        }
        column++;
    }
    int32_t row = 0;
    while (row < kRows - 1) {
        const float midpoint =
            static_cast<float>(kRowY[static_cast<size_t>(row)] + kRowY[static_cast<size_t>(row) + 1]) * 0.5f;
        if (midpoint > cursor.v) {
            break;
        }
        row++;
    }

    // Up to the nearest row with an unlocked mission, then left along it to the
    // nearest unlocked one, or failing that the first of the row.
    for (; row > 0; row--) {
        int32_t scan = 0;
        for (; scan < kColumns; scan++) {
            if (unlocked(LevelAt(row, scan))) {
                break;
            }
        }
        if (scan < kColumns) {
            break;
        }
    }
    for (; column >= 0; column--) {
        if (unlocked(LevelAt(row, column))) {
            break;
        }
    }
    if (column < 0) {
        for (column = 0; column < kColumns; column++) {
            if (unlocked(LevelAt(row, column))) {
                break;
            }
        }
    }
    return LevelAt(row, column);
}

MissionSelectModel::Next MissionSelectModel::Update(const Input::Sample& input, MenuCursor& cursor,
                                                    const MissionSelectProgress& progress) {
    sfx_.clear();
    prevTabHighlighted_ = false;
    highlighted_ = -1;

    if (CursorOnPreviousTab(cursor)) {
        prevTabHighlighted_ = true;
    } else {
        highlighted_ = FindHighlighted(cursor, progress);
    }

    if (input.Pressed(kSelectButtons)) {
        if (prevTabHighlighted_) {
            tabPrevSelected_ = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        } else if (highlighted_ >= 0) {
            briefingPage_ = EntryForLevel(highlighted_);
            selectedStage_ = static_cast<int32_t>(MissionFolderEntries()[static_cast<size_t>(briefingPage_)].stage);
            tabNextSelected_ = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (input.Pressed(kB)) {
        tabPrevSelected_ = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (tabNextSelected_) {
        const MissionFolderSetup& entry = MissionFolderEntries()[static_cast<size_t>(briefingPage_)];
        SetCursorForDifficulty(cursor, HighestUnlockedDifficulty(entry.missionNum, progress));
        return Next::Difficulty;
    }
    if (tabPrevSelected_) {
        SetCursorForMode(cursor, 0);
        return Next::ModeSelect;
    }
    return Next::Stay;
}

} // namespace Frigate::Game::Menu
