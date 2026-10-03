#include "game/menu/ModeSelectModel.h"

#include <cstdio>
#include <memory>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/CheatAvailability.h"
#include "game/menu/CheatIds.h"
#include "game/menu/FolderTabs.h"
#include "game/save/SaveProgress.h"
#include "platform/InputState.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Next = ModeSelectModel::Next;
using Highlight = ModeSelectModel::Highlight;
namespace Button = Frigate::Game::Input::Button;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Input::Sample Press(uint16_t buttons) {
    return { .pressed = buttons, .held = buttons };
}

const auto kNoCheats = [](int32_t) { return false; };
const auto kAllCheats = [](int32_t) { return true; };

// The cursor over the single player, multiplayer and cheat rows, and the tab.
void PlaceCursor(FrontState& front, float h, float v) {
    front.cursor.h = h;
    front.cursor.v = v;
}

void TestInit() {
    FrontState front;
    front.gameMode = GameMode::Cheats;
    front.tabNextSelected = true;
    front.tabPrevSelected = true;
    ModeSelectModel model;
    model.Init(front);
    Check(front.gameMode == GameMode::Intro, "init forgets the last choice");
    Check(!front.tabNextSelected && !front.tabPrevSelected, "and both tabs");
}

void TestIdleStaysPut() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 200.0f, 200.0f);
    for (int frame = 0; frame < 5; frame++) {
        Check(model.Update({}, front, 2, kNoCheats) == Next::Stay, "no input leaves the screen alone");
    }
    Check(front.cursor.h == 200.0f && front.cursor.v == 200.0f, "and the cursor where it was");
    Check(model.Sfx().empty(), "silently");
    Check(model.CurrentHighlight() == Highlight::SinglePlayer, "with single player under it");
}

void TestHighlightFollowsTheCursorRow() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);

    PlaceCursor(front, 200.0f, 242.9f);
    model.Update({}, front, 2, kAllCheats);
    Check(model.CurrentHighlight() == Highlight::SinglePlayer, "above row 243 is single player");
    PlaceCursor(front, 200.0f, 243.0f);
    model.Update({}, front, 2, kAllCheats);
    Check(model.CurrentHighlight() == Highlight::Multiplayer, "row 243 is multiplayer");
    PlaceCursor(front, 200.0f, 274.9f);
    model.Update({}, front, 2, kAllCheats);
    Check(model.CurrentHighlight() == Highlight::Multiplayer, "up to row 275");
    PlaceCursor(front, 200.0f, 275.0f);
    model.Update({}, front, 2, kAllCheats);
    Check(model.CurrentHighlight() == Highlight::Cheats, "row 275 is the cheats");
    Check(!model.PreviousTabHighlighted(), "with the tab dark");

    PlaceCursor(front, 391.0f, 224.0f);
    model.Update({}, front, 2, kAllCheats);
    Check(model.PreviousTabHighlighted() && model.CurrentHighlight() == Highlight::None,
          "the Previous tab takes the highlight from the rows");
    PlaceCursor(front, 390.0f, 300.0f);
    model.Update({}, front, 2, kAllCheats);
    Check(!model.PreviousTabHighlighted() && model.CurrentHighlight() == Highlight::Cheats,
          "the tab starts right of column 390");
    PlaceCursor(front, 400.0f, 223.0f);
    model.Update({}, front, 2, kAllCheats);
    Check(!model.PreviousTabHighlighted(), "and below row 223");
}

void TestMultiplayerNeedsASecondController() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 200.0f, 260.0f);

    Check(model.Update(Press(Button::kA), front, 1, kNoCheats) == Next::MissionSelect,
          "with one controller the multiplayer row is single player");
    Check(model.CurrentHighlight() == Highlight::SinglePlayer, "highlighted as such");

    model.Init(front);
    PlaceCursor(front, 200.0f, 260.0f);
    Check(model.Update(Press(Button::kA), front, 2, kNoCheats) == Next::MultiplayerOptions,
          "with two it goes to the multiplayer options");
    Check(front.gameMode == GameMode::Multi, "as a multiplayer game");
}

void TestCheatsNeedAnUnlockedCheat() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 200.0f, 300.0f);

    Check(model.Update(Press(Button::kStart), front, 2, kNoCheats) == Next::MultiplayerOptions,
          "with none unlocked the cheat row is multiplayer");
    Check(!model.CheatMenuAvailable(), "the cheat entry is not offered");

    model.Init(front);
    PlaceCursor(front, 200.0f, 300.0f);
    const auto onlyPaintball = [](int32_t cheat) { return cheat == Cheat::kPaintball; };
    Check(model.Update(Press(Button::kStart), front, 2, onlyPaintball) == Next::Cheat,
          "one unlocked cheat offers the cheat menu");
    Check(model.CheatMenuAvailable() && front.gameMode == GameMode::Cheats, "as a cheat game");
    Check(front.cheatAvailable[Cheat::kPaintball] && !front.cheatAvailable[Cheat::kInvincibility],
          "and records which cheat is open");
    Check(!front.cheatAvailable[Cheat::kUnused] && !front.cheatAvailable[Cheat::kInvalid],
          "the ids outside the checked range stay closed");
}

void TestCheatAvailabilityIsRecomputedEachFrame() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    model.Update({}, front, 2, kAllCheats);
    Check(model.CheatMenuAvailable() && front.cheatAvailable[Cheat::kExtraMpChars], "everything open");
    model.Update({}, front, 2, kNoCheats);
    Check(!model.CheatMenuAvailable() && !front.cheatAvailable[Cheat::kExtraMpChars], "then nothing");
}

void TestSinglePlayer() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 150.0f, 230.0f);

    for (const uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        model.Init(front);
        PlaceCursor(front, 150.0f, 230.0f);
        Check(model.Update(Press(button), front, 1, kNoCheats) == Next::MissionSelect, "A, Z and Start choose");
        Check(front.gameMode == GameMode::Solo, "a single player game");
        Check(front.cursor.h == 73.0f && front.cursor.v == 62.0f, "the cursor lands on the first mission");
        Check(model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose, "with the door sound");
    }

    model.Init(front);
    PlaceCursor(front, 150.0f, 230.0f);
    Check(model.Update(Press(Button::kCUp), front, 1, kNoCheats) == Next::Stay, "other buttons do nothing");
}

void TestPreviousTab() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 400.0f, 260.0f);

    Check(model.Update(Press(Button::kA), front, 2, kAllCheats) == Next::FileSelect, "A on the tab goes back");
    Check(front.tabPrevSelected && front.gameMode == GameMode::Intro, "without a mode chosen");
    Check(model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the other door sound");

    model.Init(front);
    PlaceCursor(front, 150.0f, 230.0f);
    Check(model.Update(Press(Button::kB), front, 2, kAllCheats) == Next::FileSelect, "B goes back from anywhere");
    Check(model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with that sound");
}

void TestEveryPressOnTheTabIsHeard() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 400.0f, 260.0f);
    model.Update(Press(Button::kA | Button::kB), front, 2, kAllCheats);
    Check(model.Sfx().size() == 2, "A and B together make both sounds, as the decomp does");
}

void TestAChoiceBeatsTheTabInTheSameFrame() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 150.0f, 230.0f);
    Check(model.Update(Press(Button::kA | Button::kB), front, 2, kAllCheats) == Next::MissionSelect,
          "a choice and B in one frame goes to the choice");
    Check(front.tabPrevSelected, "though the tab stays selected for the next screen to clear");
}

void TestCursorFollowsTheStick() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 200.0f, 200.0f);
    Input::Sample input;
    input.stickX = 45;
    input.stickY = -45;
    model.Update(input, front, 2, kNoCheats);
    Check(front.cursor.h > 200.0f && front.cursor.v > 200.0f, "the stick moves the cursor right and down");
}

void TestTheHighlightUsesTheCursorBeforeItMoves() {
    FrontState front;
    ModeSelectModel model;
    model.Init(front);
    PlaceCursor(front, 200.0f, 243.0f);
    Input::Sample input;
    input.stickY = 70;
    input.pressed = Button::kA;
    Check(model.Update(input, front, 2, kNoCheats) == Next::MultiplayerOptions,
          "the choice is made on the row the cursor was on");
}

// Cheat availability from a folder's save.
void TestCheatAvailabilityFromTheSave() {
    auto storage = std::make_shared<Frigate::Game::Save::MemorySaveStorage>();
    auto manager = std::make_shared<Frigate::Game::Save::SaveManager>(storage);
    manager->ValidateSaves();
    Frigate::Game::Save::SaveProgress progress(manager);

    for (int32_t cheat = Cheat::kExtraMpChars; cheat < Cheat::kInvalid; cheat++) {
        Check(!IsCheatAvailable(progress, 0, cheat), "a new folder has no cheat open");
    }

    progress.UnlockCheat(0, 0);
    Check(IsCheatAvailable(progress, 0, Cheat::kPaintball), "Dam's cheat is Paintball");
    Check(!IsCheatAvailable(progress, 1, Cheat::kPaintball), "in that folder only");
    progress.UnlockCheat(0, 1);
    Check(IsCheatAvailable(progress, 0, Cheat::kInvincibility), "Facility's is Invincibility");
    progress.UnlockCheat(0, 19);
    Check(IsCheatAvailable(progress, 0, Cheat::kAllGuns), "Egypt's is All Guns");
    progress.UnlockCheat(0, 5);
    Check(IsCheatAvailable(progress, 0, Cheat::kTurboMode), "Silo's is Turbo");
    progress.UnlockCheat(0, 17);
    Check(IsCheatAvailable(progress, 0, Cheat::kGoldPp7), "Cradle's is the Gold PP7");

    Check(!IsCheatAvailable(progress, 0, Cheat::kMagnum), "Magnum waits for the Cradle to be completed");
    progress.UnlockStage(0, 17, Frigate::Game::Save::kDifficultyAgent, 100);
    Check(IsCheatAvailable(progress, 0, Cheat::kMagnum), "and opens with it");
    Check(!IsCheatAvailable(progress, 0, Cheat::kLaser), "the Laser needs the Aztec on Secret Agent or better");
    Check(!IsCheatAvailable(progress, 0, Cheat::kGoldenGun), "the Golden Gun needs Egypt on 00 Agent");

    Check(!IsCheatAvailable(progress, 0, Cheat::kExtraMpChars) && !IsCheatAvailable(progress, 0, Cheat::kMaxAmmo) &&
              !IsCheatAvailable(progress, 0, Cheat::kUnlockDam),
          "the ids the front-end never offers stay closed");
    Check(!IsCheatAvailable(progress, 0, Cheat::kUnused) && !IsCheatAvailable(progress, 0, Cheat::kInvalid),
          "as do those outside the range");
}

void TestPreviousTabLayout() {
    MenuCursor cursor;
    cursor.h = 391.0f;
    cursor.v = 224.0f;
    Check(CursorOnPreviousTab(cursor), "inside the tab");
    cursor.h = 390.0f;
    Check(!CursorOnPreviousTab(cursor), "the left edge is outside");
    cursor.h = 391.0f;
    cursor.v = 223.0f;
    Check(!CursorOnPreviousTab(cursor), "and so is its top");
}

} // namespace

int main() {
    TestInit();
    TestIdleStaysPut();
    TestHighlightFollowsTheCursorRow();
    TestMultiplayerNeedsASecondController();
    TestCheatsNeedAnUnlockedCheat();
    TestCheatAvailabilityIsRecomputedEachFrame();
    TestSinglePlayer();
    TestPreviousTab();
    TestEveryPressOnTheTabIsHeard();
    TestAChoiceBeatsTheTabInTheSameFrame();
    TestCursorFollowsTheStick();
    TestTheHighlightUsesTheCursorBeforeItMoves();
    TestCheatAvailabilityFromTheSave();
    TestPreviousTabLayout();
    return gFailures == 0 ? 0 : 1;
}
