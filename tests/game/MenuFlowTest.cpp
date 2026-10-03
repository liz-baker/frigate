// The front-end's screens driven end to end: the real menu state machine and
// screen handlers (registered without their assets), fed controller samples
// through the same per-port state the game reads, over a save held in memory.

#include <array>
#include <cstdio>
#include <memory>
#include <optional>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuId.h"
#include "game/MenuStateMachine.h"
#include "game/menu/BriefingScreen.h"
#include "game/menu/CheatIds.h"
#include "game/menu/CheatMenu.h"
#include "game/menu/DifficultyScreen.h"
#include "game/menu/FileSelect.h"
#include "game/menu/FileSelectModel.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelect.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/ModeSelect.h"
#include "game/menu/StageStart.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveManager.h"
#include "game/save/SaveProgress.h"
#include "level/LevelInfo.h"
#include "platform/InputState.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
namespace Button = Frigate::Game::Input::Button;
namespace PlatformInput = Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr int32_t kFolder = 1;
constexpr int32_t kDam = 0;
constexpr int32_t kFacility = 1;
constexpr int32_t kEgypt = 19;

// The folder screens registered as the game registers them, minus assets and
// drawing, with the stage hand-off recorded where the game loop would take it.
struct Flow {
    std::shared_ptr<Save::SaveManager> manager;
    std::shared_ptr<FrontState> front = std::make_shared<FrontState>();
    MenuStateMachine menu;
    std::optional<SelectedStage> handedOff;

    Flow() {
        manager = std::make_shared<Save::SaveManager>(std::make_shared<Save::MemorySaveStorage>());
        manager->ValidateSaves();
        front->save = manager;
        front->progress = std::make_shared<Save::SaveProgress>(manager);
        front->settings = std::make_shared<Save::GameSettings>();

        RegisterFileSelect(menu, nullptr, front);
        RegisterModeSelect(menu, nullptr, front);
        RegisterMissionSelect(menu, nullptr, front);
        RegisterDifficulty(menu, nullptr, front);
        RegisterBriefing(menu, nullptr, front);
        RegisterCheatMenu(menu, nullptr, front);
        // init_menu0B_runstage's part of the hand-off: the stage and difficulty
        // the screens chose, with the folder's settings applied.
        menu.RegisterMenu(MenuId::RunStage,
                          { .onInit = [this](MenuStateMachine&) { handedOff = StartSelectedStage(*front); } });

        // Two controllers, so the mode select's multiplayer row is live.
        *PlatformInput::ConnectedControllersStorage() = 0x03;
        // The folders' hit boxes, which the game projects from the wallet
        // models: a box around where the cursor sits on each folder (stored
        // with left and up as the larger values, as the screen projects them).
        std::array<ScreenRect, 4> bounds;
        for (int32_t folder = 0; folder < 4; folder++) {
            MenuCursor on;
            FileSelectModel::PlaceCursorOnFolder(folder, on);
            bounds[static_cast<size_t>(folder)] = { on.h + 16.0f, on.v + 16.0f, on.h - 16.0f, on.v - 16.0f };
        }
        SetFolderBoundsForTesting(bounds);

        menu.ChangeMenu(MenuId::FileSelect, /*reload=*/false);
        Frame();
    }

    // One displayed frame with the first controller reading `buttons` and the
    // stick at (x, y).
    void Frame(uint16_t buttons = 0, int8_t stickX = 0, int8_t stickY = 0) {
        std::array<PlatformInput::PadSample, PlatformInput::kPortCount> samples;
        samples[0] = { stickX, stickY, buttons };
        PlatformInput::Advance(PlatformInput::Current(), samples);
        menu.Tick();
    }

    // A press and release, then frames until any screen switch has gone through.
    void Press(uint16_t buttons) {
        Frame(buttons);
        Frame();
        for (int i = 0; i < 8 && menu.CurrentMenu() == MenuId::SwitchScreens; i++) {
            Frame();
        }
    }

    MenuId Current() const {
        return menu.CurrentMenu();
    }
    Save::SaveProgress& Progress() {
        return *front->progress;
    }
    // Where the cursor is, as the next frame will read it.
    void CursorAt(float h, float v) {
        front->cursor.h = h;
        front->cursor.v = v;
    }
    // The file select to the mode select, on the folder `kFolder`.
    void ChooseFolder() {
        MenuCursor on;
        FileSelectModel::PlaceCursorOnFolder(kFolder, on);
        CursorAt(on.h, on.v);
        Press(Button::kA);
    }
    // The mode select's single player entry to the mission select.
    void ChooseSinglePlayer() {
        Press(Button::kA);
    }
    // The mission select's icon for `level`, which must be open.
    void ChooseMission(int32_t level) {
        MissionSelectModel::PlaceCursorOnStage(level, front->cursor);
        Press(Button::kA);
    }
};

int32_t StageOfLevel(int32_t level) {
    return static_cast<int32_t>(
        MissionFolderEntries()[static_cast<size_t>(MissionSelectModel::EntryForLevel(level))].stage);
}

void TestAgentPathToTheStage() {
    Flow flow;
    Check(flow.Current() == MenuId::FileSelect, "the flow starts on the file select");

    flow.ChooseFolder();
    Check(flow.Current() == MenuId::ModeSelect, "choosing a folder opens the mode select");
    Check(flow.Progress().CurrentFolder() == kFolder, "on that folder");
    Check(flow.front->gameMode == GameMode::Intro, "with no mode chosen yet");
    Check(flow.front->cursor.h == 126.0f && flow.front->cursor.v == 226.0f, "and the cursor on the first entry");

    flow.ChooseSinglePlayer();
    Check(flow.Current() == MenuId::MissionSelect, "single player opens the mission select");
    Check(flow.front->gameMode == GameMode::Solo, "as a single player game");
    Check(flow.front->selectedStage == -1 && flow.front->briefingPage == -1, "with no mission chosen yet");

    flow.ChooseMission(kDam);
    Check(flow.Current() == MenuId::Difficulty, "choosing Dam opens the difficulty select");
    Check(flow.front->selectedStage == StageOfLevel(kDam), "with its stage selected");
    Check(flow.front->briefingPage == MissionSelectModel::EntryForLevel(kDam), "and its briefing page");
    Check(flow.front->cursor.v == static_cast<float>((Save::kDifficulty00 * 0x1e) + 0xba),
          "the cursor on the highest open difficulty");

    flow.CursorAt(106.0f, static_cast<float>((Save::kDifficultyAgent * 0x1e) + 0xba));
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Briefing, "choosing Agent opens the briefing");
    Check(flow.Progress().SelectedDifficulty() == Save::kDifficultyAgent, "at that difficulty");
    Check(flow.Progress().CurrentFolder() == kFolder, "still in the same folder");

    flow.Press(Button::kStart);
    Check(flow.Current() == MenuId::RunStage, "Start leaves the briefing for the stage");
    Check(flow.handedOff.has_value(), "which is handed a stage");
    if (flow.handedOff) {
        Check(flow.handedOff->stage == StageOfLevel(kDam), "the one the mission select chose");
        Check(flow.handedOff->difficulty == Save::kDifficultyAgent, "at the difficulty chosen");
    }
}

void TestBriefingPagesBeforeTheStage() {
    Flow flow;
    flow.ChooseFolder();
    flow.ChooseSinglePlayer();
    flow.ChooseMission(kDam);
    flow.CursorAt(106.0f, static_cast<float>((Save::kDifficulty00 * 0x1e) + 0xba));
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Briefing, "00 Agent opens the briefing");

    // The Next tab turns the pages, and only Start on the last page leaves.
    flow.CursorAt(399.0f, 144.0f);
    for (int page = 0; page < 3; page++) {
        flow.Press(Button::kA);
        Check(flow.Current() == MenuId::Briefing, "Next turns a page");
        Check(!flow.handedOff.has_value(), "without starting the stage");
    }
    flow.Press(Button::kStart);
    Check(flow.Current() == MenuId::RunStage && flow.handedOff, "Start leaves from any page");
    Check(flow.handedOff && flow.handedOff->difficulty == Save::kDifficulty00, "at 00 Agent");
}

void TestOhSevenPathToTheStage() {
    Flow flow;
    // Every mission done at 00 Agent opens 007.
    for (int32_t level = kDam; level <= kEgypt; level++) {
        flow.Progress().UnlockStage(kFolder, level, Save::kDifficulty00, 100 + level);
    }
    flow.ChooseFolder();
    flow.ChooseSinglePlayer();
    flow.ChooseMission(kEgypt);
    Check(flow.Current() == MenuId::Difficulty, "a mission opens the difficulty select");
    Check(flow.front->cursor.v == static_cast<float>((Save::kDifficulty007 * 0x1e) + 0xba), "with the cursor on 007");

    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Options007, "007 opens its options");
    Check(flow.Progress().SelectedDifficulty() == Save::kDifficulty007, "at that difficulty");

    flow.Press(Button::kStart);
    Check(flow.Current() == MenuId::RunStage, "Start leaves the options for the stage");
    Check(flow.handedOff && flow.handedOff->stage == StageOfLevel(kEgypt), "the mission chosen");
    Check(flow.handedOff && flow.handedOff->difficulty == Save::kDifficulty007, "at 007");

    // The Next tab carries on to the briefing instead.
    Flow viaBriefing;
    for (int32_t level = kDam; level <= kEgypt; level++) {
        viaBriefing.Progress().UnlockStage(kFolder, level, Save::kDifficulty00, 100 + level);
    }
    viaBriefing.ChooseFolder();
    viaBriefing.ChooseSinglePlayer();
    viaBriefing.ChooseMission(kFacility);
    viaBriefing.Press(Button::kA);
    Check(viaBriefing.Current() == MenuId::Options007, "007 again");
    Check(viaBriefing.front->cursor.h == 399.0f && viaBriefing.front->cursor.v == 144.0f, "the cursor on Next");
    viaBriefing.Press(Button::kA);
    Check(viaBriefing.Current() == MenuId::Briefing, "Next opens the briefing");
    Check(viaBriefing.Progress().SelectedDifficulty() == Save::kDifficulty007, "still at 007");
}

void TestBackingOutOfEachScreen() {
    Flow flow;
    flow.ChooseFolder();
    flow.ChooseSinglePlayer();
    flow.ChooseMission(kDam);
    flow.CursorAt(106.0f, static_cast<float>((Save::kDifficultyAgent * 0x1e) + 0xba));
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Briefing, "into the briefing");

    // B on the first page goes back to the mission select, on the mission.
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::MissionSelect, "B on the first page leaves the briefing");
    Check(flow.front->cursor.h == 73.0f && flow.front->cursor.v == 62.0f, "with the cursor back on Dam's icon");
    Check(flow.Progress().CurrentFolder() == kFolder, "in the same folder");

    // Dam, then back out of the difficulty select.
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Difficulty, "Dam again");
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::MissionSelect, "B leaves the difficulty select");
    Check(flow.front->cursor.h == 73.0f && flow.front->cursor.v == 62.0f, "on Dam's icon");

    // B out of the mission select, then the mode select's Previous tab.
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::ModeSelect, "B leaves the mission select");
    Check(flow.front->cursor.h == 126.0f && flow.front->cursor.v == 226.0f, "on the single player entry");
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::FileSelect, "B leaves the mode select");

    // The file select puts the cursor on the folder it came from and lets the
    // folder be chosen again.
    MenuCursor on;
    FileSelectModel::PlaceCursorOnFolder(kFolder, on);
    Check(flow.front->cursor.h == on.h && flow.front->cursor.v == on.v, "with the cursor on the folder it left");
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::ModeSelect && flow.Progress().CurrentFolder() == kFolder, "which opens again");

    // The Previous tab and the 007 options' B.
    flow.CursorAt(400.0f, 280.0f);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::FileSelect, "the Previous tab leaves the mode select");
}

void TestOptionsBackToTheDifficultySelect() {
    Flow flow;
    for (int32_t level = kDam; level <= kEgypt; level++) {
        flow.Progress().UnlockStage(kFolder, level, Save::kDifficulty00, 100 + level);
    }
    flow.ChooseFolder();
    flow.ChooseSinglePlayer();
    flow.ChooseMission(kDam);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Options007, "007 opens its options");
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::Difficulty, "B goes back to the difficulty select");
    Check(flow.front->cursor.v == static_cast<float>((Save::kDifficulty007 * 0x1e) + 0xba), "with the cursor on 007");
    Check(!flow.handedOff.has_value(), "without starting the stage");
}

void TestLockedMissionsCannotBeChosen() {
    Flow flow;
    flow.ChooseFolder();
    flow.ChooseSinglePlayer();
    // Nothing but Dam is open in a new folder: the cursor over Facility's icon
    // picks the open one nearest it.
    MissionSelectModel::PlaceCursorOnStage(kFacility, flow.front->cursor);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Difficulty, "a press over a locked mission opens an open one");
    Check(flow.front->selectedStage == StageOfLevel(kDam), "which is Dam");
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::MissionSelect, "back on the mission select");

    // Completing Dam opens Facility.
    flow.Progress().UnlockStage(kFolder, kDam, Save::kDifficultyAgent, 100);
    MissionSelectModel::PlaceCursorOnStage(kFacility, flow.front->cursor);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Difficulty, "Facility can now be chosen");
    Check(flow.front->selectedStage == StageOfLevel(kFacility), "and is the mission selected");
}

void TestCheatsThroughToTheMissions() {
    Flow flow;
    flow.ChooseFolder();
    Check(flow.Current() == MenuId::ModeSelect, "the mode select");

    // No cheat is open in a new folder: the cheat row is not offered.
    flow.CursorAt(126.0f, 290.0f);
    flow.Press(Button::kA);
    Check(flow.Current() != MenuId::Cheat, "without a cheat there is no cheat menu");
    Check(flow.front->gameMode != GameMode::Cheats, "and no cheat game");

    // Completing Dam earns Paintball in this folder, which opens the menu.
    Flow cheating;
    cheating.Progress().UnlockCheat(kFolder, kDam);
    cheating.ChooseFolder();
    cheating.CursorAt(126.0f, 290.0f);
    cheating.Press(Button::kA);
    Check(cheating.Current() == MenuId::Cheat, "the cheat entry opens the cheat menu");
    Check(cheating.front->gameMode == GameMode::Cheats, "as a cheat game");
    Check(cheating.front->cheatAvailable[Cheat::kPaintball], "listing Paintball");

    // Switch it on with A on its row, then back out.
    cheating.CursorAt(100.0f, static_cast<float>(0x35 + 3));
    cheating.Frame(Button::kA);
    cheating.Frame();
    Check(cheating.front->cheatActivated[Cheat::kPaintball], "A switches the cheat on");
    cheating.Frame(Button::kA);
    cheating.Frame();
    Check(!cheating.front->cheatActivated[Cheat::kPaintball], "and off again");
    cheating.Frame(Button::kA);
    cheating.Frame();
    Check(cheating.front->cheatActivated[Cheat::kPaintball], "and on");

    cheating.Press(Button::kB);
    Check(cheating.Current() == MenuId::ModeSelect, "B returns to the mode select");
    Check(cheating.front->appendCheatSinglePlayer, "with the single player screens in their cheat variants");
    Check(cheating.front->cursor.h == 126.0f && cheating.front->cursor.v == 290.0f,
          "and the cursor on the cheat entry");

    // The missions then only offer what has been completed, and only on the
    // difficulties it was completed on.
    cheating.Progress().UnlockStage(kFolder, kDam, Save::kDifficultyAgent, 100);
    cheating.front->cursor.v = 226.0f;
    cheating.ChooseSinglePlayer();
    Check(cheating.Current() == MenuId::MissionSelect, "single player is the mission select");
    cheating.ChooseMission(kDam);
    Check(cheating.Current() == MenuId::Difficulty, "a finished mission opens the difficulty select");
    Check(cheating.front->cursor.v == static_cast<float>((Save::kDifficultyAgent * 0x1e) + 0xba),
          "with the cursor on the difficulty it was finished on");
    cheating.Press(Button::kA);
    Check(cheating.Current() == MenuId::Briefing, "through to the briefing");
    Check(cheating.front->cheatActivated[Cheat::kPaintball], "with the cheat still on");
    cheating.Press(Button::kStart);
    Check(cheating.Current() == MenuId::RunStage && cheating.handedOff, "and the stage");
    Check(cheating.handedOff && cheating.handedOff->stage == StageOfLevel(kDam), "that was chosen");
}

void TestMultiplayerNeedsASecondController() {
    Flow flow;
    flow.ChooseFolder();
    flow.CursorAt(126.0f, 258.0f);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::MultiplayerOptions, "two controllers open the multiplayer options");
    Check(flow.front->gameMode == GameMode::Multi, "as a multiplayer game");

    Flow single;
    *PlatformInput::ConnectedControllersStorage() = 0x01;
    single.ChooseFolder();
    single.CursorAt(126.0f, 258.0f);
    single.Press(Button::kA);
    Check(single.Current() == MenuId::MissionSelect, "with one controller the row is single player");
    *PlatformInput::ConnectedControllersStorage() = 0x03;
}

void TestFoldersKeepTheirOwnSelection() {
    Flow flow;
    // Dam is done in the first folder only.
    flow.Progress().UnlockStage(0, kDam, Save::kDifficultyAgent, 100);
    flow.ChooseFolder();
    flow.ChooseSinglePlayer();
    MissionSelectModel::PlaceCursorOnStage(kFacility, flow.front->cursor);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Difficulty && flow.front->selectedStage == StageOfLevel(kDam),
          "in the second folder Facility is shut, so Dam is chosen");

    flow.Press(Button::kB);
    flow.Press(Button::kB);
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::FileSelect, "back on the file select");
    MenuCursor first;
    FileSelectModel::PlaceCursorOnFolder(0, first);
    flow.CursorAt(first.h, first.v);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::ModeSelect && flow.Progress().CurrentFolder() == 0, "the first folder");
    flow.ChooseSinglePlayer();
    MissionSelectModel::PlaceCursorOnStage(kFacility, flow.front->cursor);
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Difficulty && flow.front->selectedStage == StageOfLevel(kFacility),
          "where Facility is open");
}

} // namespace

int main() {
    TestAgentPathToTheStage();
    TestBriefingPagesBeforeTheStage();
    TestOhSevenPathToTheStage();
    TestBackingOutOfEachScreen();
    TestOptionsBackToTheDifficultySelect();
    TestLockedMissionsCannotBeChosen();
    TestCheatsThroughToTheMissions();
    TestMultiplayerNeedsASecondController();
    TestFoldersKeepTheirOwnSelection();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::puts("MenuFlowTest passed");
    return 0;
}
