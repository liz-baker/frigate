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
#include "game/menu/LegalScreen.h"
#include "game/menu/FileSelectModel.h"
#include "game/menu/MissionEnd.h"
#include "game/menu/MissionEndModel.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelect.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/ModeSelect.h"
#include "game/menu/RunStage.h"
#include "game/menu/StageStart.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveManager.h"
#include "game/save/SaveProgress.h"
#include "level/LevelInfo.h"
#include "platform/InputState.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
namespace Level = Frigate::Level;
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
constexpr int32_t kCradle = 17;
constexpr int32_t kEgypt = 19;

// A stage in play that ends when told to, leaving the result it was given.
struct FakeStage : StageRun {
    Level::StageResult result;
    bool finished = false;
    int steps = 0;
    Level::MoveInput lastInput;

    void Step(const Level::MoveInput& input) override {
        steps++;
        lastInput = input;
    }
    bool Done() const override {
        return finished;
    }
    Level::StageResult Result() const override {
        return result;
    }
    Gfx* Render(Gfx* dl) override {
        return dl;
    }
};

// The folder screens registered as the game registers them, minus assets and
// drawing, with the stage the run-stage menu plays replaced by a FakeStage.
struct Flow {
    std::shared_ptr<Save::SaveManager> manager;
    std::shared_ptr<FrontState> front = std::make_shared<FrontState>();
    MenuStateMachine menu;
    std::shared_ptr<RunStageHost> host;
    // The stage the host was last handed, and the stage it is playing.
    std::optional<SelectedStage> handedOff;
    FakeStage* stage = nullptr;
    int stagesStarted = 0;
    bool castShown = false;

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
        RegisterMissionEnd(menu, nullptr, front);
        menu.RegisterMenu(MenuId::DisplayCast, { .onInit = [this](MenuStateMachine&) { castShown = true; } });
        // init_menu0B_runstage: the stage and difficulty the screens chose, with
        // the folder's settings applied, handed to the stage the host plays.
        host = std::make_shared<RunStageHost>(nullptr, front, DemoRunOptions{});
        host->SetStageFactory([this](const SelectedStage& selected) -> std::unique_ptr<StageRun> {
            handedOff = selected;
            stagesStarted++;
            auto fake = std::make_unique<FakeStage>();
            stage = fake.get();
            return fake;
        });
        RegisterRunStage(menu, host);

        // Two controllers, so the mode select's multiplayer row is live.
        PlatformInput::ConnectedPorts() = 0x03;
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
    // The stage ends having left `result`, and the frames to the screen it leads to.
    void FinishStage(const Level::StageResult& result) {
        stage->result = result;
        stage->finished = true;
        Frame();
        for (int i = 0; i < 8 && (Current() == MenuId::RunStage || Current() == MenuId::SwitchScreens); i++) {
            Frame();
        }
    }
    // Through the file select, mode select, mission select and difficulty
    // select to the briefing of `level` at `difficulty`, and Start.
    void StartMission(int32_t level, int32_t difficulty) {
        ChooseFolder();
        ChooseSinglePlayer();
        ChooseMission(level);
        CursorAt(106.0f, static_cast<float>((difficulty * 0x1e) + 0xba));
        Press(Button::kA);
        Press(Button::kStart);
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
    PlatformInput::ConnectedPorts() = 0x01;
    single.ChooseFolder();
    single.CursorAt(126.0f, 258.0f);
    single.Press(Button::kA);
    Check(single.Current() == MenuId::MissionSelect, "with one controller the row is single player");
    PlatformInput::ConnectedPorts() = 0x03;
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

// What a stage that ended with every objective met in `seconds` leaves.
Level::StageResult CompletedIn(int32_t seconds) {
    Level::StageResult result;
    result.exit = Level::StageExit::ReturnTitle;
    result.missionTimer = seconds * 60;
    result.killCount = 9;
    result.shots = { 50, 4, 10, 2, 0, 0, 1 };
    result.favouriteWeapon = { 2, 2 };
    result.objectives.fill(Level::ObjectiveStatus::Complete);
    result.objectivesCompleteAtExit = true;
    return result;
}

void TestCompletedMissionRecordsProgress() {
    Flow flow;
    flow.StartMission(kDam, Save::kDifficultySecret);
    Check(flow.Current() == MenuId::RunStage, "Start plays the stage");
    Check(flow.stagesStarted == 1 && flow.handedOff && flow.handedOff->difficulty == Save::kDifficultySecret,
          "once, at the difficulty chosen");
    Check(flow.host->Active(), "which the game loop draws");
    const int32_t nextBefore = flow.Progress().HighestStageUnlocked(kFolder);
    Check(flow.Progress().BestTime(kFolder, kDam, Save::kDifficultySecret) == 0, "no time is held yet");

    // The stage is stepped once a frame with the controller's reading.
    const int stepsBefore = flow.stage->steps;
    flow.Frame(Button::kA, 5, -7);
    Check(flow.stage->steps == stepsBefore + 1, "a frame steps the stage");
    Check(flow.stage->lastInput.stickX == 5 && flow.stage->lastInput.stickY == -7 &&
              flow.stage->lastInput.buttons == Button::kA,
          "with the first controller's stick and buttons");
    Check(flow.Current() == MenuId::RunStage, "and stays in play until it ends");

    // Dam's Secret Agent par time is 160 seconds.
    flow.FinishStage(CompletedIn(150));
    Check(!flow.host->Active(), "the stage is unloaded when it ends");
    Check(flow.Current() == MenuId::MissionFailed, "and the report page follows");
    Check(flow.front->missionOutcome && flow.front->missionOutcome->killCount == 9, "carrying the stage's result");
    Check(flow.Progress().BestTime(kFolder, kDam, Save::kDifficultySecret) == 150, "the time is recorded");
    Check(flow.Progress().HighestStageUnlocked(kFolder) > nextBefore, "completing Dam unlocks the next stage");
    Check(flow.Progress().IsCheatUnlocked(kFolder, kDam), "beating the par time unlocks the mission's cheat");
    Check(flow.front->newCheatUnlocked, "which the statistics page announces");

    // Next carries on to the statistics page.
    Check(flow.front->cursor.h == 399.0f && flow.front->cursor.v == 144.0f, "the cursor starts on Next");
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::MissionComplete, "Next opens the statistics");

    // Next again: a clean completion goes on to the following mission's briefing.
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::Briefing, "Next opens the next briefing");
    Check(flow.front->selectedStage == StageOfLevel(kFacility), "for Facility");
    Check(flow.front->briefingPage == MissionSelectModel::EntryForLevel(kFacility), "on its first page");
}

void TestStatisticsPreviousGoesToTheMissionList() {
    Flow flow;
    flow.StartMission(kDam, Save::kDifficultyAgent);
    flow.FinishStage(CompletedIn(300));
    Check(flow.Current() == MenuId::MissionFailed, "the report page");
    Check(flow.Progress().BestTime(kFolder, kDam, Save::kDifficultyAgent) == 300, "records the time");
    Check(!flow.Progress().IsCheatUnlocked(kFolder, kDam), "a slow time earns no cheat");
    Check(!flow.front->newCheatUnlocked, "and announces none");

    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::MissionComplete, "the statistics page");
    flow.Press(Button::kB);
    Check(flow.Current() == MenuId::MissionSelect, "B goes back to the mission list");
    Check(flow.front->cursor.h == 73.0f && flow.front->cursor.v == 62.0f, "with the cursor on Dam");

    // The report page's B goes straight back too.
    Flow report;
    report.StartMission(kDam, Save::kDifficultyAgent);
    report.FinishStage(CompletedIn(300));
    report.Press(Button::kB);
    Check(report.Current() == MenuId::MissionSelect, "B on the report page leaves for the mission list");
}

void TestDeathAndAbortRecordNothing() {
    // Killed in action with every objective met leaves by bossRunTitleStage.
    Flow dead;
    dead.StartMission(kDam, Save::kDifficultySecret);
    Level::StageResult result = CompletedIn(100);
    result.exit = Level::StageExit::RunTitle;
    result.bondKilledInAction = true;
    dead.FinishStage(result);
    Check(dead.Current() == MenuId::MissionFailed, "a death still reports");
    Check(dead.front->missionOutcome && dead.front->missionOutcome->bondKilledInAction, "killed in action");
    Check(dead.Progress().BestTime(kFolder, kDam, Save::kDifficultySecret) == 0, "without a time recorded");
    Check(!dead.Progress().IsCheatUnlocked(kFolder, kDam), "or a cheat");

    // Next on the statistics page after a failure retries the same mission's
    // briefing.
    dead.Press(Button::kA);
    Check(dead.Current() == MenuId::MissionComplete, "the statistics");
    dead.Press(Button::kA);
    Check(dead.Current() == MenuId::Briefing, "Next goes back to the briefing");
    Check(dead.front->selectedStage == StageOfLevel(kDam), "of the same mission");

    // Start plays it again, from the same selection.
    dead.Press(Button::kStart);
    Check(dead.Current() == MenuId::RunStage && dead.stagesStarted == 2, "which can be played again");

    // An abort from the watch likewise.
    Flow aborted;
    aborted.StartMission(kDam, Save::kDifficultySecret);
    result = CompletedIn(100);
    result.exit = Level::StageExit::RunTitle;
    result.aborted = true;
    aborted.FinishStage(result);
    Check(aborted.Current() == MenuId::MissionFailed, "an abort reports");
    Check(aborted.front->missionOutcome && aborted.front->missionOutcome->missionFailedOrAborted, "as aborted");
    Check(aborted.Progress().BestTime(kFolder, kDam, Save::kDifficultySecret) == 0, "and records nothing");
}

void TestCradleLeadsToCubaAndTheCredits() {
    Flow flow;
    for (int32_t level = kDam; level < kCradle; level++) {
        flow.Progress().UnlockStage(kFolder, level, Save::kDifficultyAgent, 100 + level);
    }
    flow.StartMission(kCradle, Save::kDifficultyAgent);
    Check(flow.handedOff && flow.handedOff->stage == StageOfLevel(kCradle), "Cradle plays");

    flow.FinishStage(CompletedIn(60));
    Check(flow.Current() == MenuId::MissionFailed, "its report");
    Check(flow.Progress().IsCradleCompleted(kFolder), "records Cradle as done");
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::MissionComplete, "its statistics");

    // Next after a clean Cradle runs the Cuba stage.
    flow.Press(Button::kA);
    Check(flow.Current() == MenuId::RunStage, "Next plays the Cuba stage");
    Check(flow.stagesStarted == 2 && flow.handedOff &&
              flow.handedOff->stage == static_cast<int32_t>(Level::LevelId::Cuba),
          "which is the stage handed over");

    // Cuba is not a mission: it records nothing and leads to the credits cast.
    const uint32_t cradleBest = flow.Progress().BestTime(kFolder, kCradle, Save::kDifficultyAgent);
    flow.FinishStage(CompletedIn(10));
    Check(flow.castShown, "Cuba ends on the credits cast");
    Check(flow.front->fullActorIntro, "which shows the full roster");
    Check(flow.Progress().BestTime(kFolder, kCradle, Save::kDifficultyAgent) == cradleBest, "and changes no time");
}

void TestStageThatCannotLoadReturnsToTheMissionList() {
    Flow flow;
    flow.host->SetStageFactory([&flow](const SelectedStage&) -> std::unique_ptr<StageRun> {
        flow.stagesStarted++;
        return nullptr;
    });
    flow.StartMission(kDam, Save::kDifficultyAgent);
    for (int i = 0; i < 8 && flow.Current() != MenuId::MissionSelect; i++) {
        flow.Frame();
    }
    Check(flow.stagesStarted == 1, "the load was tried");
    Check(flow.Current() == MenuId::MissionSelect, "and a stage that won't load goes back to the mission list");
    Check(!flow.host->Active(), "with nothing playing");
}

void TestLegalScreenSkip() {
    FrontState front;
    // The first pass through the legal screen ignores buttons.
    Check(!NextAfterLegalScreen(10, true, 1, front).has_value(), "the first pass ignores a button");
    Check(NextAfterLegalScreen(241, false, 1, front) == MenuId::NintendoLogo, "and times out to the Nintendo logo");
    Check(NextAfterLegalScreen(241, false, 0, front) == MenuId::NoControllers,
          "or the no-controllers screen with none connected");

    // Once it has been seen, a button skips ahead.
    front.isFirstTimeOnLegalScreen = false;
    Check(!NextAfterLegalScreen(10, false, 1, front).has_value(), "without a button it waits");
    Check(NextAfterLegalScreen(10, true, 1, front) == MenuId::NintendoLogo,
          "a button before the title sequence has played goes to the Nintendo logo");
    front.isFirstTimeOnMainMenu = false;
    Check(NextAfterLegalScreen(10, true, 1, front) == MenuId::FileSelect, "and afterwards straight to the file select");
    Check(NextAfterLegalScreen(241, false, 0, front) == MenuId::NintendoLogo,
          "a repeat pass with no controller times out to the Nintendo logo");
}

} // namespace

// The LEVELID of each LEVEL_SOLO_SEQUENCE mission (Dam .. Egypt), as bondconstants.h numbers them.
constexpr std::array<int32_t, 20> kDecompStageOfMission = { 33, 34, 35, 36, 9,  20, 26, 43, 27, 22,
                                                            24, 29, 30, 25, 37, 23, 39, 41, 28, 32 };

// Every mission at every difficulty, through the real screens, hands the stage
// factory the decomp's stage id and the difficulty the screens showed.
void TestEveryMissionAtEveryDifficultyReachesTheStage() {
    for (int32_t mission = 0; mission < 20; mission++) {
        for (int32_t difficulty = Save::kDifficultyAgent; difficulty <= Save::kDifficulty007; difficulty++) {
            Flow flow;
            for (int32_t level = kDam; level <= kEgypt; level++) {
                flow.Progress().UnlockStage(kFolder, level, Save::kDifficulty00, 100 + level);
            }
            flow.StartMission(mission, difficulty);
            Check(flow.Current() == MenuId::RunStage, "Start leaves for the stage");
            Check(flow.stagesStarted == 1 && flow.handedOff.has_value(), "which is started once");
            if (flow.handedOff) {
                Check(flow.handedOff->stage == kDecompStageOfMission[static_cast<size_t>(mission)],
                      "with the decomp's stage id for the mission");
                Check(flow.handedOff->difficulty == difficulty, "and the difficulty chosen");
            }
        }
    }
}

int main() {
    TestEveryMissionAtEveryDifficultyReachesTheStage();
    TestCompletedMissionRecordsProgress();
    TestStatisticsPreviousGoesToTheMissionList();
    TestDeathAndAbortRecordNothing();
    TestCradleLeadsToCubaAndTheCredits();
    TestStageThatCannotLoadReturnsToTheMissionList();
    TestLegalScreenSkip();
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
