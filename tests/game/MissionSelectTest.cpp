#include "game/menu/MissionSelectModel.h"

#include <cstdio>
#include <functional>
#include <map>
#include <utility>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/MissionFolders.h"
#include "game/save/SaveData.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Frigate::Game::Save::StageStatus;
namespace Button = Frigate::Game::Input::Button;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// A folder's stage statuses by (level, difficulty); anything not listed is
// locked.
struct FakeProgress : MissionSelectProgress {
    std::map<std::pair<int32_t, int32_t>, StageStatus> status;
    bool unlocked007 = false;
    bool cheat = false;

    bool Is007ModeUnlocked() const override {
        return unlocked007;
    }
    StageStatus StatusAt(int32_t level, int32_t difficulty) const override {
        const auto found = status.find({ level, difficulty });
        return found == status.end() ? StageStatus::Locked : found->second;
    }
    bool CheatVariant() const override {
        return cheat;
    }

    // The fresh folder: Dam is the one mission open, at Agent.
    static FakeProgress Fresh() {
        FakeProgress progress;
        progress.status[{ 0, 0 }] = StageStatus::Unlocked;
        return progress;
    }
    // Every mission before `level` completed and `level` open, at every
    // difficulty up to `difficulty`.
    void OpenThrough(int32_t level, int32_t difficulty) {
        for (int32_t d = 0; d <= difficulty; d++) {
            for (int32_t l = 0; l < level; l++) {
                status[{ l, d }] = StageStatus::Completed;
            }
            status[{ level, d }] = StageStatus::Unlocked;
        }
    }
};

Input::Sample Press(uint16_t buttons) {
    Input::Sample sample;
    sample.pressed = buttons;
    sample.held = buttons;
    return sample;
}

MenuCursor At(float h, float v) {
    return { h, v };
}

// The cursor on a mission's icon.
MenuCursor OnIcon(int32_t level) {
    MenuCursor cursor;
    MissionSelectModel::PlaceCursorOnStage(level, cursor);
    return cursor;
}

bool Raised(const MissionSelectModel& model) {
    return model.Sfx().size() == 1 && model.Sfx()[0] == MissionSelectSfx::DoorMetalClose2;
}

void TestEntryForLevel() {
    for (int32_t level = 0; level < MissionSelectModel::kMissionCount; level++) {
        const int32_t entry = MissionSelectModel::EntryForLevel(level);
        Check(entry >= 0 && MissionFolderEntries()[static_cast<size_t>(entry)].missionNum == level,
              "every mission is a row of mission_folder_setup_entries");
    }
    Check(MissionSelectModel::EntryForLevel(0) == 1, "Dam is the first part, after the chapter 1 heading");
    Check(MissionSelectModel::EntryForLevel(19) == 28, "Egypt is the last row before the terminator");
    Check(MissionSelectModel::EntryForLevel(20) == -1, "there is no mission 20");
    FakeProgress progress = FakeProgress::Fresh();
    Check(MissionSelectModel::HighestUnlockedDifficulty(-1, progress) == -1, "and nor is a chapter heading");
}

void TestHighestUnlockedDifficulty() {
    FakeProgress progress = FakeProgress::Fresh();
    Check(MissionSelectModel::HighestUnlockedDifficulty(0, progress) == 0, "a fresh folder opens Dam at Agent");
    Check(MissionSelectModel::HighestUnlockedDifficulty(1, progress) == -1, "and nothing else");
    Check(MissionSelectModel::HighestUnlockedDifficulty(20, progress) == -1, "a level that is no mission is locked");

    progress.OpenThrough(5, 1);
    Check(MissionSelectModel::HighestUnlockedDifficulty(0, progress) == 1,
          "completed through Secret Agent: the highest difficulty open");
    Check(MissionSelectModel::HighestUnlockedDifficulty(5, progress) == 1, "including the next mission");
    Check(MissionSelectModel::HighestUnlockedDifficulty(6, progress) == -1, "but not the one after");

    // 007 is only offered once the mode is unlocked, whatever the table says.
    progress.status[{ 0, 3 }] = StageStatus::Unlocked;
    Check(MissionSelectModel::HighestUnlockedDifficulty(0, progress) == 1, "007 is ignored while it is locked");
    progress.unlocked007 = true;
    Check(MissionSelectModel::HighestUnlockedDifficulty(0, progress) == 3, "and counted once it is unlocked");
}

void TestBonusStages() {
    FakeProgress progress = FakeProgress::Fresh();
    progress.OpenThrough(17, 1);
    progress.status[{ 17, 1 }] = StageStatus::Completed;
    progress.status[{ 18, 1 }] = StageStatus::Unlocked;
    Check(MissionSelectModel::HighestUnlockedDifficulty(17, progress) == 1, "Cradle is open");
    Check(MissionSelectModel::HighestUnlockedDifficulty(18, progress) == 1, "Aztec opens at Secret Agent");
    Check(MissionSelectModel::HighestUnlockedDifficulty(19, progress) == -1, "Egypt stays locked until 00 Agent");

    progress.status[{ 19, 2 }] = StageStatus::Unlocked;
    Check(MissionSelectModel::HighestUnlockedDifficulty(19, progress) == 2, "and opens at 00 Agent");
}

void TestCheatVariant() {
    FakeProgress progress = FakeProgress::Fresh();
    progress.OpenThrough(2, 1);
    progress.cheat = true;
    Check(MissionSelectModel::HighestUnlockedDifficulty(2, progress) == -1,
          "the cheat menu's list leaves out a mission that is only open");
    Check(MissionSelectModel::HighestUnlockedDifficulty(1, progress) == 1,
          "and lists one completed, at the highest difficulty it was");
    progress.status[{ 1, 1 }] = StageStatus::Unlocked;
    Check(MissionSelectModel::HighestUnlockedDifficulty(1, progress) == 0,
          "completed only below the highest difficulty open lists the lower");
}

void TestIconColours() {
    FakeProgress progress = FakeProgress::Fresh();
    IconColour colour = MissionSelectModel::IconVertexColour(0, 0, progress);
    Check(colour.red == 0xff && colour.green == 0xff && colour.blue == 0xff && colour.alpha == 0xf5,
          "the icon under the cursor is white and a little translucent");
    colour = MissionSelectModel::IconVertexColour(0, -1, progress);
    Check(colour.red == 0x6e && colour.green == 0x6e && colour.blue == 0x6e && colour.alpha == 0xff,
          "an open icon the cursor is not on is grey");
    colour = MissionSelectModel::IconVertexColour(1, 0, progress);
    Check(colour.red == 0xf && colour.green == 0xf && colour.blue == 0xf && colour.alpha == 0xff,
          "a locked icon is nearly black");
    colour = MissionSelectModel::IconVertexColour(1, 1, progress);
    Check(colour.red == 0xf, "even with the cursor on it");
}

void TestInit() {
    MissionSelectModel model;
    FakeProgress progress = FakeProgress::Fresh();
    MenuCursor cursor = OnIcon(0);
    model.Init();
    model.Update(Press(Button::kA), cursor, progress);
    Check(model.SelectedStage() >= 0 && model.TabNextSelected(), "choosing a mission selects its stage");

    model.Init();
    Check(model.SelectedStage() == -1 && model.BriefingPage() == -1, "init clears the stage and briefing page");
    Check(!model.TabNextSelected() && !model.TabPrevSelected(), "and the tabs");
}

void TestHighlight() {
    MissionSelectModel model;
    FakeProgress progress = FakeProgress::Fresh();
    progress.OpenThrough(7, 0);
    model.Init();

    MenuCursor cursor = OnIcon(3);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 3 && !model.PreviousTabHighlighted(), "the cursor over an open icon highlights it");

    cursor = At(73.0f, 62.0f);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 0, "the top left corner is Dam");

    // Between columns the nearer icon is the one: the midpoint of 73 and 142.
    cursor = At(107.0f, 62.0f);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 0, "short of the midpoint stays on the left icon");
    cursor = At(108.0f, 62.0f);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 1, "past it moves to the right");

    // Row 1 holds 5 to 7 open only to Surface 2; 8 and 9 are locked.
    cursor = OnIcon(9);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 7, "over a locked icon the nearest open one to its left is highlighted");

    // Row 3 is wholly locked, so the cursor falls to the row above.
    cursor = OnIcon(18);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 7, "over a locked row, the last open icon of the nearest row above");

    cursor = At(400.0f, 250.0f);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.PreviousTabHighlighted() && model.Highlighted() == -1, "the Previous tab takes the highlight");
    cursor = At(400.0f, 223.0f);
    model.Update(Input::Sample{}, cursor, progress);
    Check(!model.PreviousTabHighlighted(), "the tab starts below 223");
    cursor = At(390.0f, 250.0f);
    model.Update(Input::Sample{}, cursor, progress);
    Check(!model.PreviousTabHighlighted(), "and right of 390");

    // Open icons in a later row only: the row above is searched for the nearest.
    progress = FakeProgress::Fresh();
    progress.OpenThrough(3, 0);
    cursor = OnIcon(14);
    model.Update(Input::Sample{}, cursor, progress);
    Check(model.Highlighted() == 3, "from row 2 with nothing open it, row 0's nearest open icon is it");
}

void TestSelectMission() {
    MissionSelectModel model;
    FakeProgress progress = FakeProgress::Fresh();
    progress.OpenThrough(2, 1);
    MenuCursor cursor = OnIcon(1);
    model.Init();

    Check(model.Update(Input::Sample{}, cursor, progress) == MissionSelectModel::Next::Stay,
          "with no button the screen stays");
    Check(model.Sfx().empty(), "silently");

    using Next = MissionSelectModel::Next;
    for (uint16_t button : { Button::kA, Button::kStart, Button::kZ }) {
        model.Init();
        cursor = OnIcon(1);
        Check(model.Update(Press(button), cursor, progress) == Next::Difficulty, "A, Start and Z choose the mission");
        Check(Raised(model), "with the door sound");
    }

    model.Init();
    cursor = OnIcon(1);
    model.Update(Press(Button::kA), cursor, progress);
    Check(model.BriefingPage() == 2, "the briefing page is the mission's row (Facility)");
    Check(model.SelectedStage() == static_cast<int32_t>(MissionFolderEntries()[2].stage), "the stage is that row's");
    Check(cursor.h == 106.0f && cursor.v == 1 * 0x1e + 0xba,
          "the difficulty cursor starts on the highest difficulty open (Secret Agent)");

    model.Init();
    progress.unlocked007 = true;
    progress.status[{ 1, 3 }] = StageStatus::Unlocked;
    cursor = OnIcon(1);
    model.Update(Press(Button::kA), cursor, progress);
    Check(cursor.v == 3 * 0x1e + 0xba, "or 007 when that is open");
}

void TestBack() {
    MissionSelectModel model;
    FakeProgress progress = FakeProgress::Fresh();
    model.Init();
    using Next = MissionSelectModel::Next;

    MenuCursor cursor = OnIcon(0);
    Check(model.Update(Press(Button::kB), cursor, progress) == Next::ModeSelect, "B goes back to the mode select");
    Check(Raised(model), "with the door sound");
    Check(model.TabPrevSelected() && !model.TabNextSelected(), "through the Previous tab");
    Check(cursor.h == 126.0f && cursor.v == 0xe2, "and puts the mode select's cursor on its first choice");
    Check(model.SelectedStage() == -1, "choosing no stage");

    model.Init();
    cursor = At(400.0f, 260.0f);
    Check(model.Update(Press(Button::kA), cursor, progress) == Next::ModeSelect, "A on the Previous tab goes back");
    Check(Raised(model), "with the sound");

    // The tab wins over a mission: A on it never chooses one.
    Check(model.SelectedStage() == -1, "and chooses nothing");

    model.Init();
    cursor = OnIcon(0);
    model.Update(Press(Button::kA | Button::kB), cursor, progress);
    Check(model.TabNextSelected() && !model.TabPrevSelected(), "A takes precedence over B in the same frame");
}

void TestCursorFollowsTheStick() {
    MissionSelectModel model;
    FakeProgress progress = FakeProgress::Fresh();
    model.Init();
    MenuCursor cursor = At(200.0f, 150.0f);
    Input::Sample input;
    input.stickX = 45;
    input.stickY = -45;
    model.Update(input, cursor, progress);
    Check(cursor.h > 200.0f, "the stick moves the cursor right");
    Check(cursor.v > 150.0f, "and a stick pulled back moves it down");
}

void TestPlaceCursorOnStage() {
    MenuCursor cursor;
    MissionSelectModel::PlaceCursorOnStage(0, cursor);
    Check(cursor.h == 73.0f && cursor.v == 62.0f, "Dam is the top left icon");
    MissionSelectModel::PlaceCursorOnStage(19, cursor);
    Check(cursor.h == 352.0f && cursor.v == 270.0f, "Egypt is the bottom right");
    MissionSelectModel::PlaceCursorOnStage(8, cursor);
    Check(cursor.h == 282.0f && cursor.v == 131.0f, "and 8 is on row 1, column 3");
}

} // namespace

int main() {
    TestEntryForLevel();
    TestHighestUnlockedDifficulty();
    TestBonusStages();
    TestCheatVariant();
    TestIconColours();
    TestInit();
    TestHighlight();
    TestSelectMission();
    TestBack();
    TestCursorFollowsTheStick();
    TestPlaceCursorOnStage();
    return gFailures == 0 ? 0 : 1;
}
