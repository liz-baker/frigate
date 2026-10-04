#include "game/menu/DifficultyModel.h"
#include "game/menu/Options007Model.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "game/Sliders007.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/SaveMissionProgress.h"
#include "game/save/SaveProgress.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using namespace Frigate::Game::Save;
namespace Button = Frigate::Game::Input::Button;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) <= tolerance;
}

Input::Sample Pressing(uint16_t buttons) {
    Input::Sample sample;
    sample.pressed = buttons;
    sample.held = buttons;
    return sample;
}

Input::Sample Holding(uint16_t buttons) {
    Input::Sample sample;
    sample.held = buttons;
    return sample;
}

struct Rig {
    std::shared_ptr<MemorySaveStorage> storage = std::make_shared<MemorySaveStorage>();
    std::shared_ptr<SaveManager> manager;
    std::unique_ptr<SaveProgress> progress;

    Rig() {
        manager = std::make_shared<SaveManager>(storage);
        manager->ValidateSaves();
        progress = std::make_unique<SaveProgress>(manager);
    }
};

struct FakeSelection : DifficultySelection {
    int32_t difficulty = 1;
    int32_t Get() const override {
        return difficulty;
    }
    void Set(int32_t value) override {
        difficulty = value;
    }
};

struct DifficultyRig {
    DifficultyModel model;
    FakeSelection selection;
    MenuCursor cursor;
    bool tabNext = true;
    bool tabPrev = true;

    DifficultyRig() {
        model.Init(selection, tabNext, tabPrev);
    }
    DifficultyModel::Next Step(const Input::Sample& input, int32_t unlocked, int32_t level = 7) {
        return model.Update(input, unlocked, level, cursor, selection, tabNext, tabPrev);
    }
};

void TestInitClearsTheSelection() {
    DifficultyRig rig;
    Check(rig.selection.difficulty == kDifficultyMulti, "no difficulty is selected");
    Check(!rig.tabNext && !rig.tabPrev, "no tab is chosen");
}

void TestCursorRowsFollowWhatIsUnlocked() {
    DifficultyRig rig;
    rig.cursor = { 106.0f, 280.0f };
    rig.Step({}, kDifficulty007);
    Check(rig.model.Highlighted() == kDifficulty007, "the 007 row when it is unlocked");
    rig.Step({}, kDifficulty00);
    Check(rig.model.Highlighted() == kDifficulty00, "00 Agent when 007 is not");
    rig.cursor.v = 245.0f;
    rig.Step({}, kDifficulty00);
    Check(rig.model.Highlighted() == kDifficulty00, "00 Agent from 243");
    rig.Step({}, kDifficultySecret);
    Check(rig.model.Highlighted() == kDifficultySecret, "Secret Agent when that is the top");
    rig.cursor.v = 212.0f;
    rig.Step({}, kDifficultySecret);
    Check(rig.model.Highlighted() == kDifficultySecret, "Secret Agent from 211");
    rig.Step({}, kDifficultyAgent);
    Check(rig.model.Highlighted() == kDifficultyAgent, "Agent when nothing harder is open");
    rig.cursor.v = 100.0f;
    rig.Step({}, kDifficulty007);
    Check(rig.model.Highlighted() == kDifficultyAgent, "Agent above the rows");
    rig.Step({}, -1);
    Check(rig.model.Highlighted() == kDifficultyAgent, "and with nothing unlocked");
}

void TestPreviousTabHighlight() {
    DifficultyRig rig;
    rig.cursor = { 400.0f, 250.0f };
    rig.Step({}, kDifficulty007);
    Check(rig.model.PreviousTabHighlighted(), "the cursor on the Previous tab");
    Check(rig.model.Highlighted() == kDifficultyMulti, "highlights no difficulty");
    rig.cursor = { 389.0f, 250.0f };
    rig.Step({}, kDifficulty007);
    Check(!rig.model.PreviousTabHighlighted(), "left of the tabs it does not");
}

void TestChoosingADifficultyGoesToTheBriefing() {
    DifficultyRig rig;
    rig.cursor = { 106.0f, 215.0f };
    rig.tabNext = false;
    rig.tabPrev = false;
    const auto next = rig.Step(Pressing(Button::kA), kDifficulty00);
    Check(next == DifficultyModel::Next::Briefing, "A chooses the highlighted difficulty");
    Check(rig.selection.difficulty == kDifficultySecret, "Secret Agent is selected");
    Check(rig.tabNext, "the next tab is chosen");
    Check(rig.cursor.h == FolderTab::kNextCursorH && rig.cursor.v == FolderTab::kNextCursorV,
          "the cursor goes to the next tab");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::PaperTurn, "with the paper turn");

    for (const uint16_t button : { Button::kStart, Button::kZ }) {
        DifficultyRig other;
        other.tabNext = false;
        other.tabPrev = false;
        other.cursor = { 106.0f, 100.0f };
        Check(other.Step(Pressing(button), kDifficultyAgent) == DifficultyModel::Next::Briefing,
              "Start and Z choose too");
        Check(other.selection.difficulty == kDifficultyAgent, "Agent");
    }
}

void TestChoosing007GoesToItsOptions() {
    DifficultyRig rig;
    rig.cursor = { 106.0f, 290.0f };
    rig.tabNext = false;
    rig.tabPrev = false;
    const auto next = rig.Step(Pressing(Button::kA), kDifficulty007);
    Check(next == DifficultyModel::Next::Options007, "007 goes to the options");
    Check(rig.selection.difficulty == kDifficulty007, "007 is selected");
    Check(rig.cursor.h == FolderTab::kNextCursorH, "the cursor is on the next tab");
}

void TestNothingChosenWithAnUnlockedLevelOnly() {
    DifficultyRig rig;
    rig.tabNext = false;
    rig.tabPrev = false;
    rig.cursor = { 106.0f, 290.0f };
    // Nothing unlocked at all: the Agent row still highlights and chooses.
    Check(rig.Step(Pressing(Button::kA), -1) == DifficultyModel::Next::Briefing, "Agent is always offered");
}

void TestBackReturnsToTheMissionSelect() {
    DifficultyRig rig;
    rig.tabNext = false;
    rig.tabPrev = false;
    const auto next = rig.Step(Pressing(Button::kB), kDifficulty00, 7);
    Check(next == DifficultyModel::Next::MissionSelect, "B goes back");
    Check(rig.selection.difficulty == kDifficultyMulti, "without choosing a difficulty");
    Check(rig.cursor.h == 212.0f && rig.cursor.v == 131.0f, "the cursor goes onto the stage's icon");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door");

    DifficultyRig tab;
    tab.tabNext = false;
    tab.tabPrev = false;
    tab.cursor = { 400.0f, 300.0f };
    Check(tab.Step(Pressing(Button::kStart), kDifficulty007, 19) == DifficultyModel::Next::MissionSelect,
          "Start over the Previous tab goes back");
    Check(tab.selection.difficulty == kDifficultyMulti, "choosing nothing");
    Check(tab.cursor.h == 352.0f && tab.cursor.v == 270.0f, "onto Egypt's icon (the last one)");
}

void TestStageSoloCursorTable() {
    MenuCursor cursor;
    MissionSelectModel::PlaceCursorOnStage(0, cursor);
    Check(cursor.h == 73.0f && cursor.v == 62.0f, "the first stage");
    MissionSelectModel::PlaceCursorOnStage(5, cursor);
    Check(cursor.h == 73.0f && cursor.v == 131.0f, "the second row");
    MissionSelectModel::PlaceCursorOnStage(14, cursor);
    Check(cursor.h == 352.0f && cursor.v == 201.0f, "the third row's last");
    SetCursorForDifficulty(cursor, kDifficulty00);
    Check(cursor.h == 106.0f && cursor.v == 2 * 0x1e + 0xba, "set_cursor_pos_difficulty");
}

// The mission select's answer for `level` in `folder`.
int32_t Highest(SaveProgress& progress, int32_t folder, int32_t level, bool cheatVariant) {
    progress.SetCurrentFolder(folder);
    return MissionSelectModel::HighestUnlockedDifficulty(level, SaveMissionProgress(progress, cheatVariant));
}

void TestHighestUnlockedDifficulty() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    Check(Highest(p, 0, kLevelDam, false) == kDifficulty00, "a fresh folder's first level is open up to 00 Agent");
    Check(Highest(p, 0, kLevelFacility, false) == -1, "the second level is locked");

    p.UnlockStage(0, kLevelDam, kDifficultyAgent, 100);
    Check(Highest(p, 0, kLevelFacility, false) == kDifficultyAgent, "completing Dam opens Facility on Agent only");
    Check(Highest(p, 0, kLevelDam, false) == kDifficulty00, "Dam stays open");
    Check(Highest(p, 1, kLevelFacility, false) == -1, "other folders are untouched");

    // The cheat variants of the mission select only count completions.
    Check(Highest(p, 0, kLevelDam, true) == kDifficultyAgent,
          "a cheat variant offers the hardest difficulty completed");
    Check(Highest(p, 0, kLevelFacility, true) == -1, "and nothing for an unplayed level");

    Check(Highest(p, 0, -1, false) == -1, "no level");
    Check(Highest(p, 0, 99, false) == -1, "a level that is not a stage");

    for (int32_t level = kLevelDam; level <= kLevelEgypt; level++) {
        p.UnlockStage(0, level, kDifficulty00, 100 + level);
    }
    Check(p.Is007ModeUnlocked(0), "007 is unlocked");
    Check(Highest(p, 0, kLevelDam, false) == kDifficulty007, "so every level offers it");
    Check(Highest(p, 0, kLevelEgypt, false) == kDifficulty007, "even the last");
    Check(Highest(p, 0, kLevelDam, true) == kDifficulty007, "and the cheat variants read it");
}

struct OptionsRig {
    Options007Model model;
    Sliders007 sliders;
    MenuCursor cursor;
    bool tabNext = true;
    bool tabPrev = true;

    OptionsRig() {
        model.Init(tabNext, tabPrev);
    }
    Options007Model::Next Step(const Input::Sample& input) {
        return model.Update(input, cursor, sliders, tabNext, tabPrev);
    }
};

void TestSliderStartValuesAndModifiers() {
    const Sliders007 sliders;
    Check(sliders.reaction == 0.0f && sliders.health == 1.0f && sliders.damage == 1.0f && sliders.accuracy == 1.0f,
          "the sliders start at 0, 1, 1 and 1");

    Sliders007 set;
    set.reaction = 0.5f;
    set.health = 4.0f;
    set.damage = 2.5f;
    set.accuracy = 9.0f;
    Check(Get007ReactionSpeed(set, kDifficulty007) == 0.5f && Get007HealthMod(set, kDifficulty007) == 4.0f &&
              Get007DamageMod(set, kDifficulty007) == 2.5f && Get007AccuracyMod(set, kDifficulty007) == 9.0f,
          "007 uses the sliders");
    for (const int32_t difficulty : { kDifficultyAgent, kDifficultySecret, kDifficulty00, kDifficultyMulti }) {
        Check(Get007ReactionSpeed(set, difficulty) == 0.0f && Get007HealthMod(set, difficulty) == 1.0f &&
                  Get007DamageMod(set, difficulty) == 1.0f && Get007AccuracyMod(set, difficulty) == 1.0f,
              "no other difficulty does");
    }
}

void TestRowHighlights() {
    OptionsRig rig;
    struct Case {
        float v;
        int row; // 0 reaction, 1 accuracy, 2 damage, 3 health, 4 next tab
    };
    for (const Case& c : { Case{ 263.0f, 0 }, Case{ 0x107, 0 }, Case{ 0x106, 1 }, Case{ 0xE6, 1 }, Case{ 0xE5, 2 },
                           Case{ 0xC5, 2 }, Case{ 0xC4, 3 }, Case{ 0xA4, 3 }, Case{ 0xA3, 4 } }) {
        rig.cursor = { 200.0f, c.v };
        rig.Step({});
        const bool flags[] = { rig.model.ReactionHighlighted(), rig.model.AccuracyHighlighted(),
                               rig.model.DamageHighlighted(), rig.model.HealthHighlighted(),
                               rig.model.TabNextHighlighted() };
        bool only = true;
        for (int i = 0; i < 5; i++) {
            only = only && flags[i] == (i == c.row);
        }
        Check(only, "exactly the row under the cursor highlights");
    }

    rig.cursor = { 400.0f, 100.0f };
    rig.Step({});
    Check(rig.model.TabStartHighlighted() && !rig.model.TabNextHighlighted(), "the Start tab");
    rig.cursor = { 400.0f, 180.0f };
    rig.Step({});
    Check(rig.model.TabNextHighlighted() && !rig.model.TabStartHighlighted(), "the Next tab");
    rig.cursor = { 400.0f, 280.0f };
    rig.Step({});
    Check(rig.model.TabPrevHighlighted() && !rig.model.ReactionHighlighted(), "the Previous tab over the rows");
}

void TestDraggingSetsASlider() {
    OptionsRig rig;

    // The health row: the cursor's position along the bar, squared, times ten.
    rig.cursor = { 205.0f, 170.0f };
    rig.Step({});
    rig.Step(Holding(Button::kA));
    Check(rig.model.HealthHighlighted(), "the cursor starts over the health row");
    Check(Near(rig.sliders.health, 0.25f * 10.0f), "half way along is a quarter of the range");

    // Dragging off the row keeps the slider selected.
    rig.cursor = { 355.0f, 100.0f };
    rig.Step(Holding(Button::kA));
    Check(rig.model.HealthHighlighted(), "the highlight holds while dragging");
    Check(Near(rig.sliders.health, 10.0f), "the right end is the maximum");

    rig.cursor = { 20.0f, 100.0f };
    rig.Step(Holding(Button::kZ));
    Check(Near(rig.sliders.health, 0.0f), "and left of the bar is zero");

    // Without a button held the cursor sets nothing, and the highlight follows.
    rig.cursor = { 355.0f, 200.0f };
    rig.Step({});
    Check(Near(rig.sliders.health, 0.0f) && rig.model.DamageHighlighted(), "so the damage row highlights");
    Check(Near(rig.sliders.damage, 1.0f), "and keeps its value");

    rig.cursor = { 205.0f, 200.0f };
    rig.Step(Holding(Button::kA));
    Check(Near(rig.sliders.damage, 2.5f), "damage squares the position too");
    rig.cursor = { 205.0f, 235.0f };
    rig.Step({});
    rig.Step(Holding(Button::kA));
    Check(Near(rig.sliders.accuracy, 2.5f), "as does accuracy");

    // Reaction speed is linear, 0 to 1.
    rig.cursor = { 205.0f, 270.0f };
    rig.Step({});
    rig.Step(Holding(Button::kZ));
    Check(Near(rig.sliders.reaction, 0.5f), "reaction speed is the plain fraction");
    Check(Near(rig.sliders.health, 0.0f) && Near(rig.sliders.damage, 2.5f), "the others are untouched");
}

void TestSliderDisplayMath() {
    Check(SquaredBarWidth(10.0f) == 300, "the full bar is 300 wide");
    Check(SquaredBarWidth(2.5f) == 150, "a quarter is half the bar");
    Check(SquaredBarWidth(0.0f) == 0, "empty");
    Check(SquaredBarWidth(1.0f) == 94, "the default of 1 is a bit under a third (sqrt(0.1) * 300)");
    Check(ReactionBarWidth(1.0f) == 300 && ReactionBarWidth(0.5f) == 150 && ReactionBarWidth(0.0f) == 0,
          "the reaction bar is linear");
    Check(SliderPercent(1.0f) == 100 && SliderPercent(2.5f) == 250 && SliderPercent(10.0f) == 1000,
          "the percentages are the values times 100");
    Check(AccuracyPercent(1.0f) == 10 && AccuracyPercent(10.0f) == 100, "except accuracy's, which scales by 10");
}

void TestTabsAndTransitions() {
    {
        OptionsRig rig;
        rig.cursor = { 400.0f, 180.0f };
        rig.Step({});
        rig.tabNext = false;
        rig.tabPrev = false;
        Check(rig.Step(Pressing(Button::kA)) == Options007Model::Next::Briefing, "the Next tab goes to the briefing");
        Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with a door");
    }
    {
        OptionsRig rig;
        rig.cursor = { 400.0f, 300.0f };
        rig.Step({});
        rig.tabNext = false;
        rig.tabPrev = false;
        Check(rig.Step(Pressing(Button::kZ)) == Options007Model::Next::Difficulty, "the Previous tab goes back");
    }
    {
        OptionsRig rig;
        rig.cursor = { 200.0f, 200.0f };
        rig.tabNext = false;
        rig.tabPrev = false;
        Check(rig.Step(Pressing(Button::kB)) == Options007Model::Next::Difficulty, "B goes back");
    }
    {
        OptionsRig rig;
        rig.cursor = { 400.0f, 100.0f };
        rig.Step({});
        rig.tabNext = false;
        rig.tabPrev = false;
        Check(rig.Step(Pressing(Button::kA)) == Options007Model::Next::RunStage, "the Start tab starts the stage");
    }
    {
        // Start starts the stage wherever the cursor is.
        OptionsRig rig;
        rig.cursor = { 200.0f, 300.0f };
        rig.tabNext = false;
        rig.tabPrev = false;
        Check(rig.Step(Pressing(Button::kStart)) == Options007Model::Next::RunStage, "Start starts the stage");
        Check(rig.model.Sfx().size() == 1, "with a door");
    }
    {
        // A on a slider row raises the sound but leaves the screen.
        OptionsRig rig;
        rig.cursor = { 200.0f, 200.0f };
        rig.tabNext = false;
        rig.tabPrev = false;
        Check(rig.Step(Pressing(Button::kA)) == Options007Model::Next::Stay, "A on a slider stays");
        Check(rig.model.Sfx().size() == 1, "and clicks");
    }
}

void TestTabCursorChecks() {
    MenuCursor cursor = { 390.0f, 50.0f };
    Check(!CursorOnStartTab(cursor), "the left edge is outside the tabs");
    cursor.h = 390.5f;
    Check(CursorOnStartTab(cursor), "the Start tab");
    cursor.v = 130.5f;
    Check(CursorOnStartTab(cursor) && !CursorOnNextTab(cursor), "its bottom edge is its own");
    cursor.v = 130.6f;
    Check(!CursorOnStartTab(cursor) && CursorOnNextTab(cursor), "then the Next tab");
    cursor.v = 223.0f;
    Check(CursorOnNextTab(cursor) && !CursorOnPreviousTab(cursor), "to its bottom edge");
    cursor.v = 223.1f;
    Check(CursorOnPreviousTab(cursor), "then Previous");
    SetCursorToNextTab(cursor);
    Check(cursor.h == 399.0f && cursor.v == 144.0f && CursorOnNextTab(cursor), "the next tab's cursor spot");
}

} // namespace

int main() {
    TestInitClearsTheSelection();
    TestCursorRowsFollowWhatIsUnlocked();
    TestPreviousTabHighlight();
    TestChoosingADifficultyGoesToTheBriefing();
    TestChoosing007GoesToItsOptions();
    TestNothingChosenWithAnUnlockedLevelOnly();
    TestBackReturnsToTheMissionSelect();
    TestStageSoloCursorTable();
    TestHighestUnlockedDifficulty();
    TestSliderStartValuesAndModifiers();
    TestRowHighlights();
    TestDraggingSetsASlider();
    TestSliderDisplayMath();
    TestTabsAndTransitions();
    TestTabCursorChecks();
    return gFailures == 0 ? 0 : 1;
}
