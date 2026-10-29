#include "game/menu/FileSelectModel.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderScreenSpace.h"
#include "game/menu/MissionFolders.h"
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

bool Near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
}

struct FakeSave : FileSelectSave {
    int32_t current = -1;
    int32_t previous = 0;
    // The highest stage and difficulty completed in each folder.
    std::array<int32_t, 4> stage = { -1, -1, -1, -1 };
    std::array<int32_t, 4> difficulty = { -1, -1, -1, -1 };
    std::vector<int32_t> deleted;
    std::vector<int32_t> copied;

    int32_t CurrentFolder() const override {
        return current;
    }
    void SetCurrentFolder(int32_t folder) override {
        current = folder;
    }
    int32_t PreviousFolder() const override {
        return previous;
    }
    void HighestCompleted(int32_t folder, int32_t* outStage, int32_t* outDifficulty) const override {
        *outStage = stage[static_cast<size_t>(folder)];
        *outDifficulty = difficulty[static_cast<size_t>(folder)];
    }
    void DeleteFolder(int32_t folder) override {
        deleted.push_back(folder);
    }
    void CopyFolderToFirstFree(int32_t folder) override {
        copied.push_back(folder);
    }
};

Input::Sample Press(uint16_t buttons) {
    Input::Sample sample;
    sample.pressed = buttons;
    sample.held = buttons;
    return sample;
}

Input::Sample Stick(int8_t x, int8_t y) {
    Input::Sample sample;
    sample.stickX = x;
    sample.stickY = y;
    return sample;
}

// Folder 0 occupies x 100..200, y 50..150; folder 1 x 250..350 on the same
// rows; 2 and 3 are below them, clear of the icons at y 271..299. The boxes are stored swapped, as the
// projection leaves them.
ScreenRect FolderBox(int32_t folder) {
    const float x = (folder % 2 == 0) ? 100.0f : 250.0f;
    const float y = (folder < 2) ? 50.0f : 160.0f;
    ScreenRect rect;
    rect.right = x;
    rect.left = x + 100.0f;
    rect.down = y;
    rect.up = y + 100.0f;
    return rect;
}

FileSelectLayout MakeLayout() {
    FileSelectLayout layout;
    layout.folderBounds = FolderBox;
    layout.copy = { 209.0f, 271.0f, 280.0f, 299.0f };
    layout.erase = { 319.0f, 271.0f, 390.0f, 299.0f };
    return layout;
}

struct Rig {
    FileSelectModel model;
    FakeSave save;
    MenuCursor cursor;
    FileSelectLayout layout = MakeLayout();

    Rig() {
        save.current = 0;
        model.Init(save);
        // The first frame puts the cursor on the current folder and clears it.
        model.Update(Input::Sample{}, 0, cursor, layout, save);
    }

    FileSelectModel::Next Update(const Input::Sample& input, int timer = 0) {
        return model.Update(input, timer, cursor, layout, save);
    }
    void PutCursor(float h, float v) {
        cursor.h = h;
        cursor.v = v;
    }
    bool Raised(FileSelectSfx sfx) const {
        for (FileSelectSfx raised : model.Sfx()) {
            if (raised == sfx) {
                return true;
            }
        }
        return false;
    }
};

void TestInit() {
    FakeSave save;
    FileSelectModel model;
    save.current = -1;
    model.Init(save);
    Check(save.current == 0, "init_menu05_fileselect makes the first folder current when none is");

    save.current = 3;
    model.Init(save);
    Check(save.current == 3, "and keeps a folder that is");
    Check(model.FolderSelectedForDeletion() == -1, "no erase is pending");
    Check(model.DeletionChoice() == 1, "the erase prompt starts on cancel");
}

void TestIdleTimer() {
    Check(!FileSelectModel::ResetsIdleTimer(Input::Sample{}), "no input leaves the timer running");
    Check(FileSelectModel::ResetsIdleTimer(Press(Button::kA)), "a button press resets it");
    Check(FileSelectModel::ResetsIdleTimer(Press(Button::kCUp)), "any button does");
    Check(!FileSelectModel::ResetsIdleTimer(Stick(5, -5)), "a stick within the dead zone does not");
    Check(FileSelectModel::ResetsIdleTimer(Stick(6, 0)), "a stick past it on the right does");
    Check(FileSelectModel::ResetsIdleTimer(Stick(-6, 0)), "left does");
    Check(FileSelectModel::ResetsIdleTimer(Stick(0, 6)), "up does");
    Check(FileSelectModel::ResetsIdleTimer(Stick(0, -6)), "down does");

    Rig rig;
    Check(rig.Update(Input::Sample{}, 1800) == FileSelectModel::Next::Stay, "30 seconds is not yet the timeout");
    Check(rig.Update(Input::Sample{}, 1801) == FileSelectModel::Next::LegalScreen,
          "30 seconds and a frame go back to the legal screen");
}

void TestFirstFrameFindsTheCurrentFolder() {
    Rig rig;
    Check(rig.save.current == -1, "the current folder is cleared once the cursor is on it");
    const auto expected = FolderScreenSpace::ProjectToScreen(FolderScreenSpace::CameraSpacePosition(0));
    Check(Near(rig.cursor.h, expected.x - 1.0f) && Near(rig.cursor.v, expected.y + 20.0f),
          "the cursor sits just below the folder's centre");

    // Coming back from the mode select: the demo folder maps to the one before it.
    FakeSave save;
    FileSelectModel model;
    MenuCursor cursor;
    FileSelectLayout layout = MakeLayout();
    save.current = Save::kRamRomFolderNum;
    save.previous = 2;
    model.Update(Input::Sample{}, 0, cursor, layout, save);
    const auto third = FolderScreenSpace::ProjectToScreen(FolderScreenSpace::CameraSpacePosition(2));
    Check(Near(cursor.h, third.x - 1.0f) && Near(cursor.v, third.y + 20.0f), "the demo folder uses the previous one");

    save.current = 9;
    model.Update(Input::Sample{}, 0, cursor, layout, save);
    const auto first = FolderScreenSpace::ProjectToScreen(FolderScreenSpace::CameraSpacePosition(0));
    Check(Near(cursor.h, first.x - 1.0f), "a folder out of range becomes the first");
}

void TestFolderPositions() {
    using namespace FolderScreenSpace;
    const Point2 first = ProjectToScreen(CameraSpacePosition(0));
    const Point2 second = ProjectToScreen(CameraSpacePosition(1));
    const Point2 third = ProjectToScreen(CameraSpacePosition(2));
    const Point2 fourth = ProjectToScreen(CameraSpacePosition(3));
    Check(first.x < second.x && Near(first.y, second.y), "the first two folders share a row");
    Check(third.x < fourth.x && Near(third.y, fourth.y) && third.y > first.y, "the others are below");
    Check(Near(first.x, 155.7f, 0.5f) && Near(first.y, 107.8f, 0.5f),
          "the first folder's centre on the 440x330 screen");
    Check(Near(second.x, 348.6f, 0.5f), "the second folder is to the right");
    Check(Near(CameraSpacePosition(1)[2], -4000.0f), "folders are 4000 units from the camera");
}

void TestSelectingAFolder() {
    for (uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        Rig rig;
        rig.PutCursor(300.0f, 100.0f);
        const auto next = rig.Update(Press(button));
        Check(next == FileSelectModel::Next::ModeSelect, "a select button on a folder continues to the mode select");
        Check(rig.save.current == 1, "the folder under the cursor is current");
        Check(Near(rig.cursor.h, 126.0f) && Near(rig.cursor.v, 226.0f), "the cursor is placed for the mode select");
        Check(rig.Raised(FileSelectSfx::PaperTurn), "the paper turns");
    }

    Rig rig;
    rig.PutCursor(150.0f, 250.0f);
    rig.Update(Press(Button::kA));
    Check(rig.save.current == 2, "the third folder is the one on the lower left");

    Rig outside;
    outside.PutCursor(230.0f, 20.0f);
    Check(outside.Update(Press(Button::kA)) == FileSelectModel::Next::Stay, "between the folders nothing is chosen");
    Check(outside.save.current == -1, "no folder becomes current");

    Rig other;
    other.PutCursor(300.0f, 100.0f);
    Check(other.Update(Press(Button::kB)) == FileSelectModel::Next::Stay, "other buttons choose nothing");
}

void TestCursorFollowsTheStick() {
    Rig rig;
    rig.PutCursor(200.0f, 150.0f);
    rig.Update(Stick(0, 0));
    Check(Near(rig.cursor.h, 200.0f) && Near(rig.cursor.v, 150.0f), "no stick leaves it");
    rig.Update(Stick(5, -5));
    Check(Near(rig.cursor.h, 200.0f) && Near(rig.cursor.v, 150.0f), "so does the dead zone");

    rig.Update(Stick(30, 0));
    Check(Near(rig.cursor.h, 200.0f + (25.0f * 0.075f + 0.5f)), "the stick right moves it right");
    rig.PutCursor(200.0f, 150.0f);
    rig.Update(Stick(-30, 0));
    Check(Near(rig.cursor.h, 200.0f + (-25.0f * 0.075f - 0.5f)), "left moves it left");
    rig.PutCursor(200.0f, 150.0f);
    rig.Update(Stick(0, 30));
    Check(rig.cursor.v < 150.0f, "the stick up moves it up the screen");
    rig.PutCursor(200.0f, 150.0f);
    rig.Update(Stick(0, -30));
    Check(rig.cursor.v > 150.0f, "down moves it down");

    rig.PutCursor(200.0f, 150.0f);
    rig.Update(Stick(127, 0));
    Check(Near(rig.cursor.h, 200.0f + (70.0f * 0.075f + 0.5f)), "a full stick is held to 70");
    const float before = rig.cursor.h;
    rig.Update(Stick(-128, 0));
    Check(rig.cursor.h < before, "the stick's far end is a valid reading");

    rig.PutCursor(419.0f, 311.0f);
    rig.Update(Stick(100, -100));
    Check(Near(rig.cursor.h, 420.0f) && Near(rig.cursor.v, 310.0f),
          "the cursor stops 20 pixels from the right and bottom");
    rig.PutCursor(21.0f, 21.0f);
    rig.Update(Stick(-100, 100));
    Check(Near(rig.cursor.h, 20.0f) && Near(rig.cursor.v, 20.0f), "and from the left and top");
}

Input::Sample Pointer(float u, float v, uint16_t pressed = 0) {
    Input::Sample sample = Press(pressed);
    sample.pointer = { true, u, v };
    return sample;
}

void TestCursorFollowsThePointer() {
    MenuCursor cursor;
    cursor.h = 10.0f;
    cursor.v = 10.0f;
    Check(!FileSelectModel::MoveCursorToPointer(Input::Sample{}, cursor), "no pointer, no move");
    Check(cursor.h == 10.0f && cursor.v == 10.0f, "and the cursor stays");

    Check(FileSelectModel::MoveCursorToPointer(Pointer(0.5f, 0.5f), cursor), "the pointer sets the cursor");
    Check(Near(cursor.h, 220.0f) && Near(cursor.v, 165.0f), "the middle of the 440x330 screen");
    FileSelectModel::MoveCursorToPointer(Pointer(0.25f, 0.75f), cursor);
    Check(Near(cursor.h, 110.0f) && Near(cursor.v, 247.5f), "a quarter across, three quarters down");

    FileSelectModel::MoveCursorToPointer(Pointer(0.0f, 0.0f), cursor);
    Check(Near(cursor.h, 20.0f) && Near(cursor.v, 20.0f), "the corner stops at the decomp's margin");
    FileSelectModel::MoveCursorToPointer(Pointer(1.0f, 1.0f), cursor);
    Check(Near(cursor.h, 420.0f) && Near(cursor.v, 310.0f), "so does the far corner");

    // The stick has no say while the pointer holds the cursor.
    Input::Sample both = Pointer(0.5f, 0.5f);
    both.stickX = 70;
    FileSelectModel::MoveCursorToPointer(both, cursor);
    Check(Near(cursor.h, 220.0f), "the pointer sets it, not the stick");
}

// A click is A, and the folder it chooses is the one the pointer is over.
void TestClickChoosesTheFolderUnderThePointer() {
    Rig rig;
    FileSelectModel::MoveCursorToPointer(Pointer(300.0f / 440.0f, 100.0f / 330.0f), rig.cursor);
    Check(rig.Update(Press(Button::kA)) == FileSelectModel::Next::ModeSelect, "a click on a folder continues");
    Check(rig.save.current == 1, "the folder under the pointer is current");

    Rig away;
    FileSelectModel::MoveCursorToPointer(Pointer(230.0f / 440.0f, 20.0f / 330.0f), away.cursor);
    Check(away.Update(Press(Button::kA)) == FileSelectModel::Next::Stay, "a click between the folders chooses none");
}

void TestCopy() {
    Rig rig;
    rig.PutCursor(240.0f, 285.0f);
    rig.Update(Press(Button::kA));
    Check(rig.model.OptionIcon() == 1, "a press on Copy picks up the copy icon");
    Check(rig.Raised(FileSelectSfx::DoorLock), "with a lock sound");

    rig.PutCursor(150.0f, 100.0f);
    const auto next = rig.Update(Press(Button::kA));
    Check(next == FileSelectModel::Next::Stay, "copying does not choose the folder");
    Check(rig.save.copied.size() == 1 && rig.save.copied[0] == 0, "the folder is copied to the first free one");
    Check(rig.model.OptionIcon() == 0, "and the cursor goes back to a crosshair");
    Check(rig.Raised(FileSelectSfx::CopyFile), "with the copy sound");
    Check(rig.save.current == -1, "the folder is not entered");

    rig.PutCursor(240.0f, 285.0f);
    rig.Update(Press(Button::kZ));
    rig.Update(Press(Button::kB));
    Check(rig.model.OptionIcon() == 0, "B puts the icon down");
    Check(rig.Raised(FileSelectSfx::GunM60AmmGun3), "with the click");

    rig.PutCursor(240.0f, 285.0f);
    rig.Update(Press(Button::kStart));
    rig.PutCursor(230.0f, 10.0f);
    rig.Update(Press(Button::kA));
    Check(rig.model.OptionIcon() == 0, "a press on nothing puts it down too");
    Check(rig.save.copied.size() == 1, "without copying");
}

void TestErase() {
    Rig rig;
    rig.save.stage[1] = 3;
    rig.save.difficulty[1] = 1;

    rig.PutCursor(350.0f, 285.0f);
    rig.Update(Press(Button::kA));
    Check(rig.model.OptionIcon() == 2, "a press on Erase picks up the erase icon");

    rig.PutCursor(300.0f, 100.0f);
    rig.Update(Press(Button::kA));
    Check(rig.model.FolderSelectedForDeletion() == 1, "a press on a folder with progress asks to confirm");
    Check(rig.model.DeletionChoice() == 1, "with cancel chosen");
    Check(rig.model.OptionIcon() == 0, "the icon is put down");
    Check(rig.Raised(FileSelectSfx::OptionClick2), "with the option click");

    // The prompt ignores the folders and the cursor stays put.
    rig.Update(Press(Button::kPadRight));
    Check(rig.model.DeletionChoice() == 0, "right chooses confirm");
    Check(rig.Raised(FileSelectSfx::OptionClick2), "with a click");
    rig.Update(Press(Button::kPadLeft));
    Check(rig.model.DeletionChoice() == 1, "left chooses cancel");
    rig.Update(Press(Button::kR));
    Check(rig.model.DeletionChoice() == 0, "R chooses confirm");
    rig.Update(Press(Button::kL));
    Check(rig.model.DeletionChoice() == 1, "L chooses cancel");
    rig.Update(Press(Button::kCRight));
    Check(rig.model.DeletionChoice() == 0, "C right chooses confirm");
    rig.Update(Press(Button::kCLeft));
    Check(rig.model.DeletionChoice() == 1, "C left chooses cancel");

    rig.Update(Stick(0x2E, 0));
    Check(rig.model.DeletionChoice() == 0, "the stick right chooses confirm");
    rig.Update(Stick(0x2E, 0));
    Check(rig.model.DeletionChoice() == 0, "and stays there");
    rig.Update(Stick(-0x2D, 0));
    Check(rig.model.DeletionChoice() == 0, "not quite far enough left");
    rig.Update(Stick(-0x2E, 0));
    Check(rig.model.DeletionChoice() == 1, "the stick left chooses cancel");

    rig.Update(Press(Button::kA));
    Check(rig.save.deleted.empty(), "confirming with cancel chosen erases nothing");
    Check(rig.model.FolderSelectedForDeletion() == -1, "and closes the prompt");
    Check(rig.Raised(FileSelectSfx::GunM60AmmGun3), "with the click");
    const auto folder = FolderScreenSpace::ProjectToScreen(FolderScreenSpace::CameraSpacePosition(1));
    Check(Near(rig.cursor.h, folder.x - 1.0f) && Near(rig.cursor.v, folder.y + 20.0f),
          "the cursor returns to the folder");
}

void TestEraseConfirmed() {
    Rig rig;
    rig.save.stage[0] = 0;
    rig.save.difficulty[0] = 0;
    rig.PutCursor(350.0f, 285.0f);
    rig.Update(Press(Button::kZ));
    rig.PutCursor(150.0f, 100.0f);
    rig.Update(Press(Button::kZ));
    Check(rig.model.FolderSelectedForDeletion() == 0, "a folder with only Dam completed on Agent can be erased");
    rig.Update(Press(Button::kPadRight));
    rig.Update(Press(Button::kStart));
    Check(rig.save.deleted.size() == 1 && rig.save.deleted[0] == 0, "confirm erases the folder");
    Check(rig.model.FolderSelectedForDeletion() == -1, "and closes the prompt");

    // B cancels without a choice.
    rig.PutCursor(350.0f, 285.0f);
    rig.Update(Press(Button::kA));
    rig.PutCursor(150.0f, 100.0f);
    rig.Update(Press(Button::kA));
    rig.Update(Press(Button::kPadRight));
    rig.Update(Press(Button::kB));
    Check(rig.save.deleted.size() == 1, "B erases nothing even with confirm chosen");
    Check(rig.model.FolderSelectedForDeletion() == -1, "and closes the prompt");

    // A folder with nothing done has nothing to erase.
    rig.PutCursor(350.0f, 285.0f);
    rig.Update(Press(Button::kA));
    rig.PutCursor(300.0f, 100.0f);
    rig.Update(Press(Button::kA));
    Check(rig.model.FolderSelectedForDeletion() == -1, "a blank folder is not asked about");
    Check(rig.model.OptionIcon() == 0, "though the icon is put down");

    // The prompt does not move the cursor with the stick or choose folders.
    rig.save.stage[2] = 5;
    rig.save.difficulty[2] = 2;
    rig.PutCursor(350.0f, 285.0f);
    rig.Update(Press(Button::kA));
    rig.PutCursor(150.0f, 250.0f);
    rig.Update(Press(Button::kA));
    Check(rig.model.FolderSelectedForDeletion() == 2, "a folder in the lower row");
    rig.PutCursor(300.0f, 100.0f);
    rig.Update(Stick(0, 0));
    Check(rig.save.current == -1, "while the prompt is up nothing is chosen");
    const float h = rig.cursor.h;
    rig.Update(Stick(60, 0));
    Check(Near(rig.cursor.h, h), "and the stick does not move the cursor");
}

void TestInputSample() {
    Check(Input::Sample::CrossedAbove(10, 20, 15), "a stick passing a threshold going up");
    Check(!Input::Sample::CrossedAbove(20, 25, 15), "not once it is past");
    Check(Input::Sample::CrossedBelow(-10, -20, 15), "going down");
    Check(!Input::Sample::CrossedBelow(-20, -25, 15), "not once it is past");

    Frigate::Platform::Input::PortState port;
    port.pressed = Button::kA;
    port.buttons = Button::kA | Button::kB;
    port.stickX = 7;
    port.stickY = -8;
    port.prevStickX = 3;
    port.prevStickY = 4;
    const Input::Sample sample = Input::Sample::FromPort(port);
    Check(sample.Pressed(Button::kA) && !sample.Pressed(Button::kB), "pressed this frame");
    Check(sample.Held(Button::kB), "and held");
    Check(sample.stickX == 7 && sample.stickY == -8 && sample.prevStickX == 3 && sample.prevStickY == 4,
          "the stick now and before");
    Check(sample.StickXCrossedAbove(5) && !sample.StickYCrossedAbove(5), "the axes cross on their own readings");
}

void TestMissionFolders() {
    const auto entries = MissionFolderEntries();
    Check(entries.size() == 30 && entries.back().label == nullptr, "the table ends with its terminator");
    Check(ChapterBriefingEntry(2) == 0, "Facility sits under chapter 1");
    Check(ChapterBriefingEntry(0) == 0, "a heading is its own chapter");
    Check(ChapterBriefingEntry(8) == 7, "so is a part's chapter the heading before it");

    size_t cradle = 0;
    while (entries[cradle].folderTextSlot != 0 && entries[cradle].missionNum != Save::kLevelCradle) {
        cradle++;
    }
    Check(std::string_view(entries[cradle].label) == "iv", "Cradle is part iv");
    Check(std::string_view(entries[static_cast<size_t>(ChapterBriefingEntry(static_cast<int32_t>(cradle)))].label) ==
              "7",
          "of chapter 7");
}

void TestTitleStageLoad() {
    FrontState front;
    Check(MenuAfterTitleStageLoad(front) == MenuId::LegalScreen, "an unattended demo end returns to the legal screen");
    front.prevKeypresses = true;
    Check(MenuAfterTitleStageLoad(front) == MenuId::FileSelect, "a button press returns to the file select");
}

} // namespace

int main() {
    TestInit();
    TestIdleTimer();
    TestFirstFrameFindsTheCurrentFolder();
    TestFolderPositions();
    TestSelectingAFolder();
    TestCursorFollowsTheStick();
    TestCursorFollowsThePointer();
    TestClickChoosesTheFolderUnderThePointer();
    TestCopy();
    TestErase();
    TestEraseConfirmed();
    TestInputSample();
    TestMissionFolders();
    TestTitleStageLoad();
    return gFailures == 0 ? 0 : 1;
}
