#include "game/menu/CheatMenuModel.h"

#include <array>
#include <cstdio>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/CheatTable.h"
#include "game/save/SaveData.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
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
    Input::Sample sample;
    sample.pressed = buttons;
    sample.held = buttons;
    return sample;
}

Input::Sample Hold(uint16_t buttons) {
    Input::Sample sample;
    sample.held = buttons;
    return sample;
}

// A front-end with `count` cheats available, the first ids from the second
// one up so the list's order is not just the array's.
FrontState FrontWithCheats(int count) {
    FrontState front;
    int32_t cheat = Cheat::kInvincibility;
    for (int i = 0; i < count; i++, cheat++) {
        front.cheatAvailable[static_cast<size_t>(cheat)] = true;
    }
    return front;
}

// The cursor over row `row` of the left column, and of the right.
void CursorOnLeftRow(FrontState& front, int row) {
    front.cursor.h = 100.0f;
    front.cursor.v = static_cast<float>(CheatMenuModel::kRowTop + (row * CheatMenuModel::kRowHeight) + 3);
}
void CursorOnRightRow(FrontState& front, int row) {
    front.cursor.h = 250.0f;
    front.cursor.v = static_cast<float>(CheatMenuModel::kRowTop + (row * CheatMenuModel::kRowHeight) + 3);
}

void TestCheatTable() {
    using namespace Cheat;
    Check(MenuTextSlot(kInvincibility) == 0, "invincibility is named by LMISC slot 0");
    Check(MenuTextSlot(kMagnum) == 0x0A, "the magnum by slot 0xA");
    Check(MenuTextSlot(kInfiniteAmmo) == 0x06, "infinite ammo by slot 6");
    Check(MenuTextSlot(k2xLaser) == 0x3F, "2x laser by slot 0x3F");
    Check(MenuTextSlot(kNoRadarMp) == 0x43, "no radar by slot 0x43");
    Check(!MenuTextSlot(kExtraMpChars).has_value(), "a cheat with no text bank id has no name");
    Check(!MenuTextSlot(0).has_value() && !MenuTextSlot(75).has_value(), "ids outside the table have none");

    Check(IsMultiplayerCheat(kInvincibility), "invincibility applies in multiplayer");
    Check(!IsMultiplayerCheat(kAllGuns), "all guns does not");
    Check(!IsMultiplayerCheat(kInvisibility), "nor does invisibility");
    Check(IsMultiplayerCheat(kNoRadarMp), "no radar does");
    Check(!IsMultiplayerCheat(kMagnum), "the magnum does not");
    Check(IsMultiplayerCheat(kInfiniteAmmo), "infinite ammo does");
    Check(!IsMultiplayerCheat(kEnemyRockets), "enemy rockets does not");
    Check(!IsMultiplayerCheat(0), "an id outside the table does not");
}

void TestInit() {
    FrontState front = FrontWithCheats(3);
    front.tabNextSelected = true;
    front.tabPrevSelected = true;
    CheatMenuModel model;
    model.Init(front);
    Check(model.TotalUnlocked() == 3, "the list holds the available cheats");
    Check(model.UnlockedCheat(0) == Cheat::kInvincibility && model.UnlockedCheat(1) == Cheat::kAllGuns &&
              model.UnlockedCheat(2) == Cheat::kMaxAmmo,
          "in id order");
    Check(!front.tabNextSelected && !front.tabPrevSelected, "init clears the tab selections");

    front.cheatAvailable[Cheat::kInvincibility] = false;
    model.Init(front);
    Check(model.TotalUnlocked() == 2 && model.UnlockedCheat(0) == Cheat::kAllGuns, "and rebuilds it on each visit");

    // cheathighlighted is a global: it survives a revisit.
    CursorOnLeftRow(front, 1);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 1, "the second row is highlighted");
    model.Init(front);
    Check(model.Highlighted() == 1, "and stays so across init");
}

void TestHighlightOneColumn() {
    FrontState front = FrontWithCheats(5);
    CheatMenuModel model;
    model.Init(front);

    CursorOnLeftRow(front, 0);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 0, "the cursor on the first row highlights it");

    CursorOnLeftRow(front, 3);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 3, "and on the fourth the fourth");

    front.cursor.v = static_cast<float>(CheatMenuModel::kRowTop + (3 * CheatMenuModel::kRowHeight));
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 3, "a row starts at its top edge");
    front.cursor.v = static_cast<float>(CheatMenuModel::kRowTop + (3 * CheatMenuModel::kRowHeight) - 1);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 2, "and not a pixel above it");

    CursorOnLeftRow(front, 9);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 4, "below the last row highlights the last");

    front.cursor.v = 10.0f;
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 0, "above the first row highlights the first");

    CursorOnRightRow(front, 2);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 2, "a single column ignores which side the cursor is on");
}

void TestHighlightTwoColumns() {
    FrontState front = FrontWithCheats(15);
    CheatMenuModel model;
    model.Init(front);

    CursorOnLeftRow(front, 4);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 4, "the left column holds twelve");

    CursorOnLeftRow(front, 11);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 11, "up to its last row");
    CursorOnLeftRow(front, 14);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 11, "and no further down it");

    CursorOnRightRow(front, 0);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 12, "the right column's first row is the thirteenth cheat");
    CursorOnRightRow(front, 2);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 14, "its last is the fifteenth");
    CursorOnRightRow(front, 8);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 14, "and nothing is below it");

    front.cursor.h = static_cast<float>(CheatMenuModel::kSecondColumnCursorX);
    front.cursor.v = 100.0f;
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 14, "the right column starts at x 220");
    front.cursor.h = 219.9f;
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 2, "and not before it");

    // Above the right column's first row the highlight falls back to the left
    // column's own rows.
    front.cursor.h = 300.0f;
    front.cursor.v = 20.0f;
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 0, "above the first row, over the right column, is the first row");
}

void TestHighlightHeldButton() {
    FrontState front = FrontWithCheats(5);
    CheatMenuModel model;
    model.Init(front);
    CursorOnLeftRow(front, 1);
    model.Update(Input::Sample{}, front);
    Check(model.Highlighted() == 1, "row 1 is highlighted");

    CursorOnLeftRow(front, 3);
    model.Update(Hold(Button::kA), front);
    Check(model.Highlighted() == 1, "the highlight stays while A is held");
    model.Update(Hold(Button::kZ), front);
    Check(model.Highlighted() == 1, "and while Z is");
    model.Update(Hold(Button::kB), front);
    Check(model.Highlighted() == 3, "but B held does not stop it following");
}

void TestToggle() {
    FrontState front = FrontWithCheats(5);
    CheatMenuModel model;
    model.Init(front);
    CursorOnLeftRow(front, 2);
    model.Update(Input::Sample{}, front);
    const int32_t third = model.UnlockedCheat(2);

    Check(model.Update(Press(Button::kA), front) == CheatMenuModel::Next::Stay, "A on a cheat stays on the screen");
    Check(front.cheatActivated[static_cast<size_t>(third)], "and switches it on");
    for (int32_t cheat = 1; cheat < static_cast<int32_t>(kCheatInvalid); cheat++) {
        if (cheat != third) {
            Check(!front.cheatActivated[static_cast<size_t>(cheat)], "no other cheat");
        }
    }
    Check(model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose2, "the door closes");

    model.Update(Hold(Button::kA), front);
    Check(front.cheatActivated[static_cast<size_t>(third)], "holding A does not toggle again");
    model.Update(Press(Button::kA), front);
    Check(!front.cheatActivated[static_cast<size_t>(third)], "pressing A again switches it off");

    model.Update(Press(Button::kZ), front);
    Check(front.cheatActivated[static_cast<size_t>(third)], "Z switches it on too");

    // A press frame acts on the row highlighted the frame before: the held
    // buttons stop the highlight following the cursor.
    CursorOnLeftRow(front, 0);
    model.Update(Press(Button::kA), front);
    Check(!front.cheatActivated[static_cast<size_t>(third)] &&
              !front.cheatActivated[static_cast<size_t>(model.UnlockedCheat(0))],
          "a press frame acts on the row highlighted the frame before");
    model.Update(Input::Sample{}, front);
    model.Update(Press(Button::kA), front);
    Check(front.cheatActivated[static_cast<size_t>(model.UnlockedCheat(0))], "and the next press on the new row");

    CursorOnLeftRow(front, 1);
    model.Update(Input::Sample{}, front);
    model.Update(Press(Button::kA), front);
    Check(front.cheatActivated[static_cast<size_t>(model.UnlockedCheat(1))] &&
              front.cheatActivated[static_cast<size_t>(model.UnlockedCheat(0))],
          "cheats can be on together");

    model.Update(Press(Button::kStart), front);
    Check(model.Sfx().empty(), "Start does nothing here");
}

void TestToggleInSecondColumn() {
    FrontState front = FrontWithCheats(15);
    CheatMenuModel model;
    model.Init(front);
    CursorOnRightRow(front, 1);
    model.Update(Input::Sample{}, front);
    model.Update(Press(Button::kA), front);
    Check(front.cheatActivated[static_cast<size_t>(model.UnlockedCheat(13))],
          "the right column's second row is the fourteenth cheat");
    int count = 0;
    for (bool on : front.cheatActivated) {
        count += on ? 1 : 0;
    }
    Check(count == 1, "and only it");
}

void TestAppendFlags() {
    FrontState front = FrontWithCheats(3);
    CheatMenuModel::UpdateAppendFlags(front);
    Check(!front.appendCheatSinglePlayer && !front.appendCheatMultiPlayer, "no cheat on, no variants");

    front.cheatActivated[Cheat::kAllGuns] = true;
    CheatMenuModel::UpdateAppendFlags(front);
    Check(front.appendCheatSinglePlayer && !front.appendCheatMultiPlayer,
          "a single-player cheat gives the single-player variant only");

    front.cheatActivated[Cheat::kInvincibility] = true;
    CheatMenuModel::UpdateAppendFlags(front);
    Check(front.appendCheatSinglePlayer && front.appendCheatMultiPlayer, "one that applies in multiplayer gives both");

    front.cheatActivated[Cheat::kAllGuns] = false;
    CheatMenuModel::UpdateAppendFlags(front);
    Check(front.appendCheatSinglePlayer && front.appendCheatMultiPlayer, "the multiplayer one alone gives both");

    front.cheatActivated[Cheat::kInvincibility] = false;
    CheatMenuModel::UpdateAppendFlags(front);
    Check(!front.appendCheatSinglePlayer && !front.appendCheatMultiPlayer, "all off clears them");

    // The flags are not touched by toggling; update_menu15_cheat sets them as
    // the screen is left.
    FrontState toggled = FrontWithCheats(3);
    CheatMenuModel model;
    model.Init(toggled);
    CursorOnLeftRow(toggled, 0);
    model.Update(Input::Sample{}, toggled);
    model.Update(Press(Button::kA), toggled);
    Check(!toggled.appendCheatSinglePlayer, "toggling leaves the flags to the update");
}

void TestBackToModeSelect() {
    FrontState front = FrontWithCheats(5);
    front.gameMode = GameMode::Cheats;
    CheatMenuModel model;
    model.Init(front);
    CursorOnLeftRow(front, 1);
    model.Update(Input::Sample{}, front);

    Check(model.Update(Press(Button::kB), front) == CheatMenuModel::Next::ModeSelect, "B goes back to the mode select");
    Check(front.tabPrevSelected, "with the previous tab selected");
    Check(model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose2, "the door closes");
    Check(front.cursor.h == 126.0f &&
              front.cursor.v == static_cast<float>((static_cast<int32_t>(GameMode::Cheats) * 0x20) + 0xE2),
          "the cursor lands on the cheat option");
    int on = 0;
    for (bool cheat : front.cheatActivated) {
        on += cheat ? 1 : 0;
    }
    Check(on == 0, "B toggles nothing");

    FrontState onTab = FrontWithCheats(5);
    onTab.gameMode = GameMode::Cheats;
    CheatMenuModel tabModel;
    tabModel.Init(onTab);
    onTab.cursor.h = 400.0f;
    onTab.cursor.v = 260.0f;
    tabModel.Update(Input::Sample{}, onTab);
    Check(tabModel.PreviousTabHighlighted(), "the cursor on the previous tab highlights it");
    Check(tabModel.Update(Press(Button::kA), onTab) == CheatMenuModel::Next::ModeSelect, "A on the tab goes back");
    on = 0;
    for (bool cheat : onTab.cheatActivated) {
        on += cheat ? 1 : 0;
    }
    Check(on == 0, "without toggling a cheat");

    FrontState zOnTab = FrontWithCheats(5);
    CheatMenuModel zModel;
    zModel.Init(zOnTab);
    zOnTab.cursor.h = 400.0f;
    zOnTab.cursor.v = 260.0f;
    zModel.Update(Input::Sample{}, zOnTab);
    Check(zModel.Update(Press(Button::kZ), zOnTab) == CheatMenuModel::Next::ModeSelect, "Z on the tab goes back");
}

void TestPreviousTabEdges() {
    MenuCursor cursor;
    cursor.h = 390.1f;
    cursor.v = 223.1f;
    Check(CursorOnPreviousTab(cursor), "just inside the tab's corner is on it");
    cursor.h = 390.0f;
    Check(!CursorOnPreviousTab(cursor), "its left edge is not");
    cursor.h = 400.0f;
    cursor.v = 223.0f;
    Check(!CursorOnPreviousTab(cursor), "nor its top edge");

    // Over the tab no row is highlighted and the tab lights up.
    FrontState front = FrontWithCheats(5);
    CheatMenuModel model;
    model.Init(front);
    front.cursor.h = 400.0f;
    front.cursor.v = 300.0f;
    model.Update(Input::Sample{}, front);
    Check(model.PreviousTabHighlighted() && model.Highlighted() == 0, "the tab highlight replaces the row highlight");
    CursorOnLeftRow(front, 2);
    model.Update(Input::Sample{}, front);
    Check(!model.PreviousTabHighlighted() && model.Highlighted() == 2, "and leaving it restores the row");

    // The tab highlight is only recomputed while neither A nor Z is held.
    front.cursor.h = 400.0f;
    front.cursor.v = 300.0f;
    model.Update(Hold(Button::kA), front);
    Check(!model.PreviousTabHighlighted(), "holding A freezes the tab highlight");
}

void TestStickMovesCursor() {
    FrontState front = FrontWithCheats(5);
    CheatMenuModel model;
    model.Init(front);
    front.cursor.h = 100.0f;
    front.cursor.v = 100.0f;
    Input::Sample sample;
    sample.stickX = 60;
    sample.stickY = -60;
    model.Update(sample, front);
    Check(front.cursor.h > 100.0f && front.cursor.v > 100.0f, "the stick moves the cursor right and down");

    front.cursor.h = 100.0f;
    front.cursor.v = 100.0f;
    sample.stickX = 3;
    sample.stickY = 3;
    model.Update(sample, front);
    Check(front.cursor.h == 100.0f && front.cursor.v == 100.0f, "inside the dead zone it does not");
}

} // namespace

int main() {
    TestCheatTable();
    TestInit();
    TestHighlightOneColumn();
    TestHighlightTwoColumns();
    TestHighlightHeldButton();
    TestToggle();
    TestToggleInSecondColumn();
    TestAppendFlags();
    TestBackToModeSelect();
    TestPreviousTabEdges();
    TestStickMovesCursor();
    return gFailures == 0 ? 0 : 1;
}
