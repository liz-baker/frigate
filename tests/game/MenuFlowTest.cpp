// The front-end's screens driven end to end: the real menu state machine and
// screen handlers (registered without their assets), fed controller samples
// through the same per-port state the game reads, over a save held in memory.

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "RecordingMenuAudio.h"
#include "game/DevCheats.h"
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
#include "game/menu/MenuAudio.h"
#include "game/menu/MenuCheats.h"
#include "game/menu/MissionEnd.h"
#include "game/menu/MissionEndModel.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelect.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/ModeSelect.h"
#include "game/menu/MpCharSelectModel.h"
#include "game/menu/MpCharSelectScreen.h"
#include "game/menu/MpControlStyleScreen.h"
#include "game/menu/MpHandicapModel.h"
#include "game/menu/MpHandicapScreen.h"
#include "game/menu/MpOptionsModel.h"
#include "game/menu/MpOptionsScreen.h"
#include "game/menu/MpScenarioSelectModel.h"
#include "game/menu/MpScenarioSelectScreen.h"
#include "game/menu/MpStageSelectModel.h"
#include "game/menu/MpStageSelectScreen.h"
#include "game/menu/MpTeamsScreen.h"
#include "game/menu/MpCharSelectModel.h"
#include "game/menu/MpCharSelectScreen.h"
#include "game/menu/MpControlStyleScreen.h"
#include "game/menu/MpHandicapModel.h"
#include "game/menu/MpHandicapScreen.h"
#include "game/menu/MpOptionsModel.h"
#include "game/menu/MpOptionsScreen.h"
#include "game/menu/MpScenarioSelectModel.h"
#include "game/menu/MpScenarioSelectScreen.h"
#include "game/menu/MpStageSelectModel.h"
#include "game/menu/MpStageSelectScreen.h"
#include "game/menu/MpTeamsScreen.h"
#include "game/menu/RunStage.h"
#include "game/menu/StageStart.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveManager.h"
#include "game/save/SaveProgress.h"
#include "level/CheatInput.h"
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

using Frigate::AudioDriver::kSfxDoorMetalClose2;

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
    // The players a multiplayer game has, and the pads they were last stepped with.
    int players = 1;
    int padSteps = 0;
    PlatformInput::State lastPads{};
    int lastControllers = 0;

    void Step(const Level::MoveInput& input) override {
        steps++;
        lastInput = input;
    }
    void StepPads(const PlatformInput::State& pads, int controllers) override {
        padSteps++;
        lastPads = pads;
        lastControllers = controllers;
        StageRun::StepPads(pads, controllers);
    }
    int PlayerCount() const override {
        return players;
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
    RecordingMenuAudio audio;

    ~Flow() {
        SetMenuAudio(nullptr);
    }
    FakeStage* stage = nullptr;
    int stagesStarted = 0;
    bool castShown = false;

    explicit Flow(bool multiplayerScreens = false) {
        SetMenuAudio(&audio);
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
        if (multiplayerScreens) {
            RegisterMpOptions(menu, nullptr, front);
            RegisterMpCharSelect(menu, nullptr, front);
            RegisterMpScenarioSelect(menu, nullptr, front);
            RegisterMpStageSelect(menu, nullptr, front);
            RegisterMpHandicap(menu, nullptr, front);
            RegisterMpControlStyle(menu, nullptr, front);
            RegisterMpTeams(menu, nullptr, front);
        }
        menu.RegisterMenu(MenuId::DisplayCast, { .onInit = [this](MenuStateMachine&) { castShown = true; } });
        // init_menu0B_runstage: the stage and difficulty the screens chose, with
        // the folder's settings applied, handed to the stage the host plays.
        host = std::make_shared<RunStageHost>(nullptr, front, DemoRunOptions{});
        host->SetStageFactory([this](const SelectedStage& selected) -> std::unique_ptr<StageRun> {
            handedOff = selected;
            stagesStarted++;
            auto fake = std::make_unique<FakeStage>();
            if (front->gameMode == GameMode::Multi) {
                fake->players = front->multiplayer.playerCount;
            }
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

    // One displayed frame with every port reading its entry of `buttons`.
    void FramePorts(const std::array<uint16_t, PlatformInput::kPortCount>& buttons) {
        std::array<PlatformInput::PadSample, PlatformInput::kPortCount> samples;
        for (size_t port = 0; port < samples.size(); port++) {
            samples[port] = { 0, 0, buttons[port] };
        }
        PlatformInput::Advance(PlatformInput::Current(), samples);
        menu.Tick();
    }
    // Presses on several ports at once, then the release and any screen switch.
    void PressPorts(const std::array<uint16_t, PlatformInput::kPortCount>& buttons) {
        FramePorts(buttons);
        Frame();
        for (int i = 0; i < 8 && menu.CurrentMenu() == MenuId::SwitchScreens; i++) {
            Frame();
        }
    }
    // The cursor on a point and a frame for the screen to see it there, then A.
    void ChooseAt(float h, float v) {
        CursorAt(h, v);
        Frame();
        Press(Button::kA);
    }
    // Frames until the screen is `id`; false if it is not reached.
    bool WaitFor(MenuId id, int limit = 60) {
        for (int i = 0; i < limit && Current() != id; i++) {
            Frame();
        }
        return Current() == id;
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
    Check(flow.audio.folderMusic == 1, "which starts the folders' music");

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

    const size_t cuesBefore = flow.audio.sfx.size();
    flow.Press(Button::kStart);
    Check(flow.audio.sfx.size() == cuesBefore + 1 && flow.audio.sfx.back() == kSfxDoorMetalClose2,
          "Start closes the folder door (DOOR_METAL_CLOSE2_SFX)");
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

// A multiplayer game, however it ends (the scores dismissed, or an abort), goes
// back to the multiplayer options and records nothing in the folder.
void TestMultiplayerGameReturnsToItsOptions() {
    for (int players = 2; players <= 4; players++) {
        Flow flow;
        flow.ChooseFolder();
        flow.CursorAt(126.0f, 258.0f);
        flow.Press(Button::kA);
        Check(flow.Current() == MenuId::MultiplayerOptions, "the multiplayer options open");

        Check(SelectMultiplayerStage(*flow.front, MultiplayerSettings::Defaults(players)), "a stage is picked");
        flow.menu.ChangeMenu(MenuId::RunStage, /*reload=*/true);
        for (int i = 0; i < 8 && flow.stage == nullptr; i++) {
            flow.Frame();
        }
        Check(flow.stage != nullptr, "the multiplayer stage starts");

        flow.stage->result.exit = Level::StageExit::RunTitle;
        flow.FinishStage(flow.stage->result);
        Check(flow.Current() == MenuId::MultiplayerOptions, "the game ends back at the multiplayer options");
        Check(flow.front->gameMode == GameMode::Multi, "still in a multiplayer game");
        Check(flow.front->missionOutcome == nullptr, "with no mission result");
    }
}

// The cheats a multiplayer game carries are the cheat menu's choice, and the
// screens' cheat variants follow them as update_menu15_cheat does.
void TestMultiplayerStartCarriesCheats() {
    FrontState front;
    MultiplayerSettings settings = MultiplayerSettings::Defaults(3);
    Check(SelectMultiplayerStage(front, settings) && !front.appendCheatSinglePlayer && !front.appendCheatMultiPlayer,
          "no cheats, no cheat variants");
    settings.cheats = { Cheat::kAllGuns };
    Check(SelectMultiplayerStage(front, settings) && front.cheatActivated[Cheat::kAllGuns] &&
              front.appendCheatSinglePlayer && !front.appendCheatMultiPlayer,
          "a cheat only for solo games is on without the multiplayer variant");
    settings.cheats = { Cheat::kAllGuns, Cheat::kNoRadarMp };
    Check(SelectMultiplayerStage(front, settings) && front.cheatActivated[Cheat::kNoRadarMp] &&
              front.appendCheatMultiPlayer,
          "one that applies in multiplayer gives the multiplayer variant");
    settings.cheats = { Cheat::kInvalid };
    Check(!SelectMultiplayerStage(front, settings), "an id outside the table is refused");
}

// A multiplayer game through every screen with a pad per player: the mode
// select, the options and each screen they open, Start, the stage ticking with
// every player's pad, and the end back at the options with the choices kept.
void TestMultiplayerMenusToTheStageAndBack() {
    for (int players = 2; players <= 4; players++) {
        Flow flow(/*multiplayerScreens=*/true);
        PlatformInput::ConnectedPorts() = 0x0F;
        const std::string prefix = std::to_string(players) + " players: ";
        const auto check = [&prefix](bool condition, const char* what) { Check(condition, (prefix + what).c_str()); };
        MultiplayerSettings& settings = flow.front->multiplayer;

        flow.ChooseFolder();
        flow.CursorAt(126.0f, 258.0f);
        flow.Press(Button::kA);
        check(flow.Current() == MenuId::MultiplayerOptions, "the mode select opens the multiplayer options");
        check(settings.playerCount == 4, "the first visit counts the four controllers");

        // The Players row steps 2, 3, 4 and round.
        const int presses = players == 4 ? 0 : players - 1;
        for (int i = 0; i < presses; i++) {
            flow.ChooseAt(200.0f, 0x79);
        }
        check(settings.playerCount == players, "the Players row chooses the player count");

        // Each player locks in the character four rows along.
        flow.ChooseAt(200.0f, MpOptionsModel::kCharacterRow);
        check(flow.Current() == MenuId::MultiplayerCharacterSelect, "the Character row opens the character select");
        for (int step = 0; step < 4; step++) {
            flow.PressPorts({ Button::kPadRight, Button::kPadRight, Button::kPadRight, Button::kPadRight });
        }
        flow.PressPorts({ Button::kA, Button::kA, Button::kA, Button::kA });
        check(flow.WaitFor(MenuId::MultiplayerOptions), "once everyone has locked in the options return");
        for (int player = 0; player < players; player++) {
            check(settings.character[static_cast<size_t>(player)] == player + 4,
                  "each player has the character chosen");
        }

        // Player p steps their handicap down p + 1 rows.
        flow.ChooseAt(200.0f, MpOptionsModel::kHealthRow);
        check(flow.Current() == MenuId::MultiplayerHandicap, "the Health row opens the handicap screen");
        for (int step = 0; step < players; step++) {
            std::array<uint16_t, 4> left{};
            for (int player = step; player < players; player++) {
                left[static_cast<size_t>(player)] = Button::kPadLeft;
            }
            flow.PressPorts(left);
        }
        flow.PressPorts({ Button::kA, Button::kA, Button::kA, Button::kA });
        check(flow.WaitFor(MenuId::MultiplayerOptions), "the handicaps locked in return to the options");
        for (int player = 0; player < players; player++) {
            check(settings.handicap[static_cast<size_t>(player)] == MultiplayerSettings::kNormalHandicap - 1 - player,
                  "each player has their own handicap");
        }

        // Player p steps their control style p rows along.
        flow.ChooseAt(200.0f, MpOptionsModel::kControlStyleRow);
        check(flow.Current() == MenuId::MultiplayerControlStyle, "the Control Style row opens its screen");
        for (int step = 1; step < players; step++) {
            std::array<uint16_t, 4> right{};
            for (int player = step; player < players; player++) {
                right[static_cast<size_t>(player)] = Button::kPadRight;
            }
            flow.PressPorts(right);
        }
        flow.PressPorts({ Button::kA, Button::kA, Button::kA, Button::kA });
        check(flow.WaitFor(MenuId::MultiplayerOptions), "the styles locked in return to the options");
        for (size_t player = 1; player < static_cast<size_t>(players); player++) {
            check(settings.controlStyle[player] != settings.controlStyle[player - 1],
                  "each player has their own control style");
        }

        // The stage: Random for two players, one the fresh save has open otherwise.
        const MpStage stages[] = { MpStage::Random, MpStage::Random, MpStage::Library, MpStage::Caves };
        const MpStage stage = stages[players - 1];
        flow.ChooseAt(200.0f, MpOptionsModel::kStageRow);
        check(flow.Current() == MenuId::MultiplayerStageSelect, "the Stage row opens the stage select");
        const int cell = static_cast<int>(stage);
        const float cellTop[] = { 100.0f, 175.0f, 245.0f };
        flow.ChooseAt(static_cast<float>(MpStageSelectModel::kColumnLeft[cell % MpStageSelectModel::kColumns]) + 10.0f,
                      cellTop[cell / MpStageSelectModel::kColumns]);
        check(flow.WaitFor(MenuId::MultiplayerOptions), "the choice returns to the options");
        check(settings.stage == stage, "with the stage chosen");

        // The scenario: a plain one for two players, the teams for three and four.
        const MpScenario scenarios[] = { MpScenario::Normal, MpScenario::YouOnlyLiveTwice, MpScenario::TwoVsOne,
                                         MpScenario::TwoVsTwo };
        const MpScenario scenario = scenarios[players - 1];
        flow.ChooseAt(200.0f, MpOptionsModel::kScenarioRow);
        check(flow.Current() == MenuId::MultiplayerScenarioSelect, "the Scenario row opens the scenario select");
        flow.ChooseAt(200.0f, static_cast<float>(MpScenarioSelectModel::kFirstRowTop +
                                                 static_cast<int>(scenario) * MpScenarioSelectModel::kRowSpacing));
        if (scenario == MpScenario::TwoVsOne || scenario == MpScenario::TwoVsTwo) {
            check(flow.Current() == MenuId::MultiplayerTeams, "a team scenario opens the team select");
            flow.Press(Button::kA);
        }
        check(flow.WaitFor(MenuId::MultiplayerOptions), "the scenario returns to the options");
        check(settings.scenario == scenario, "with the scenario chosen");

        // Start.
        flow.Press(Button::kStart);
        check(flow.WaitFor(MenuId::RunStage), "Start leaves the options for the stage");
        check(flow.stagesStarted == 1 && flow.stage != nullptr, "which starts one stage");
        check(flow.front->gameMode == GameMode::Multi, "as a multiplayer game");
        check(flow.front->multiplayer.playerCount == players, "for the players chosen");
        const auto level = static_cast<Level::LevelId>(flow.front->selectedStage);
        if (stage == MpStage::Random) {
            check(flow.front->multiplayer.stage == MpStage::Random, "a Random stage stays Random in the settings");
            bool open = false;
            for (int row = 1; row < kMpStageCount; row++) {
                MultiplayerSettings probe;
                probe.stage = static_cast<MpStage>(row);
                open = open || (probe.StageLevel() == level &&
                                MultiplayerUnlocks::IsStageUnlocked(flow.Progress(), row, players));
            }
            check(open, "and the stage drawn is one that is open for the players");
        } else {
            MultiplayerSettings probe;
            probe.stage = stage;
            check(probe.StageLevel() == level, "the stage chosen is the one played");
        }

        // A few ticks, each player's pad read by the stage.
        FakeStage* playing = flow.stage;
        check(playing->players == players, "the stage has a view for every player");
        for (int tick = 0; tick < 5; tick++) {
            flow.FramePorts({ Button::kA, 0, Button::kB, 0 });
        }
        check(playing->padSteps >= 5, "the stage ticks with the pads");
        check((playing->lastPads[0].buttons & Button::kA) != 0 && (playing->lastPads[2].buttons & Button::kB) != 0,
              "each from its own port");
        check(playing->lastControllers == 4, "with the controller count the menus saw");

        // The end: back to the options with every choice kept.
        const MultiplayerSettings chosen = settings;
        playing->result.exit = Level::StageExit::RunTitle;
        flow.FinishStage(playing->result);
        check(flow.Current() == MenuId::MultiplayerOptions, "the game ends back at the options");
        check(flow.front->gameMode == GameMode::Multi, "still a multiplayer game");
        check(settings.playerCount == chosen.playerCount && settings.stage == chosen.stage &&
                  settings.scenario == chosen.scenario && settings.character == chosen.character &&
                  settings.handicap == chosen.handicap && settings.controlStyle == chosen.controlStyle &&
                  settings.teamSize == chosen.teamSize,
              "and the settings the same for the next game");

        // The next game plays them without the screens being visited again.
        flow.Press(Button::kStart);
        check(flow.WaitFor(MenuId::RunStage) && flow.stagesStarted == 2, "Start plays a second game");
        flow.stage->result.exit = Level::StageExit::RunTitle;
        flow.FinishStage(flow.stage->result);
        check(flow.Current() == MenuId::MultiplayerOptions, "which also ends at the options");

        // B backs out to the mode select.
        flow.Press(Button::kB);
        check(flow.Current() == MenuId::ModeSelect, "B leaves the options for the mode select");
        PlatformInput::ConnectedPorts() = 0x03;
    }
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

// Types a cheat code into the menus one button a frame, as a player would: the
// triggers held, and the one other button going down.
void TypeMenuCode(FrontState& front, const uint16_t* code, int buttons = 10) {
    for (int i = 0; i < buttons; i++) {
        const uint16_t triggers = code[i] & (Button::kL | Button::kR);
        TickMenuCheatInput(front, code[i], code[i] & ~triggers, /*inTitle=*/true);
    }
}

void TestMenuCheatCodes() {
    namespace Cheat = Frigate::Game::Menu::Cheat;
    using Frigate::AudioDriver::kSfxCameraBeep1;
    RecordingMenuAudio audio;
    SetMenuAudio(&audio);
    FrontState front;
    front.save = std::make_shared<Save::SaveManager>(std::make_shared<Save::MemorySaveStorage>());
    front.save->ValidateSaves();
    front.progress = std::make_shared<Save::SaveProgress>(front.save);

    // mBtnCheatFacility, as the decomp lists it.
    const uint16_t facility[10] = {
        Button::kL | Button::kR | Button::kCUp, Button::kR | Button::kCLeft,
        Button::kL | Button::kPadLeft,          Button::kR | Button::kCUp,
        Button::kL | Button::kPadLeft,          Button::kR | Button::kCDown,
        Button::kL | Button::kCRight,           Button::kR | Button::kPadRight,
        Button::kL | Button::kR | Button::kCUp, Button::kL | Button::kPadRight,
    };
    const uint16_t* table = Level::CheatCodeFor(Cheat::kUnlockFacility);
    bool same = table != nullptr;
    for (int i = 0; same && i < 10; i++) {
        same = table[i] == facility[i];
    }
    Check(same, "the Facility unlock code is the decomp's");

    // No folder chosen yet: an unlock has nowhere to go.
    front.progress->SetCurrentFolder(-1);
    TypeMenuCode(front, Level::CheatCodeFor(Cheat::kUnlockPaintball));
    Check(audio.sfx.empty(), "an unlock code with no folder chosen does nothing");

    front.progress->SetCurrentFolder(kFolder);
    TypeMenuCode(front, Level::CheatCodeFor(Cheat::kUnlockPaintball));
    Check(front.progress->IsCheatUnlocked(kFolder, 0), "a cheat unlock code unlocks its cheat in the folder");
    Check(audio.sfx == std::vector<int>{ kSfxCameraBeep1 }, "and beeps");

    // An in-stage code does nothing in the menus.
    audio.sfx.clear();
    TypeMenuCode(front, Level::CheatCodeFor(Cheat::kLineMode));
    Check(audio.sfx.empty(), "an in-stage code is not read in the menus");

    // A stage code records the stage before its own as completed on Agent.
    TypeMenuCode(front, Level::CheatCodeFor(Cheat::kUnlockDam));
    Check(!front.progress->IsStageCompleted(kFolder, kDam, Save::kDifficultyAgent) && audio.sfx.empty(),
          "Dam's own code does nothing");
    TypeMenuCode(front, facility);
    Check(front.progress->IsStageCompleted(kFolder, kDam, Save::kDifficultyAgent) &&
              audio.sfx == std::vector<int>{ kSfxCameraBeep1 },
          "Facility's code completes Dam");
    Check(front.progress->IsStageUnlocked(kFolder, kFacility, Save::kDifficultyAgent), "which opens Facility");
    audio.sfx.clear();
    TypeMenuCode(front, facility);
    Check(audio.sfx.empty(), "Facility's code does nothing once Dam is completed");
    TypeMenuCode(front, Level::CheatCodeFor(Cheat::kUnlockRunway));
    Check(front.progress->IsStageCompleted(kFolder, kFacility, Save::kDifficultyAgent),
          "Runway's code completes Facility once Dam is");

    TypeMenuCode(front, Level::CheatCodeFor(Cheat::kExtraMpChars));
    Check(front.mpUnlocks.NumCharsSelectable() == kMpCharsAll, "the extra characters code opens every character");

    // A stage running in between empties the buffer.
    audio.sfx.clear();
    const uint16_t* dkMode = Level::CheatCodeFor(Cheat::kUnlockDkMode);
    TypeMenuCode(front, dkMode, 9);
    TickMenuCheatInput(front, 0, 0, /*inTitle=*/false);
    const uint16_t triggers = dkMode[9] & (Button::kL | Button::kR);
    TickMenuCheatInput(front, dkMode[9], dkMode[9] & ~triggers, /*inTitle=*/true);
    Check(audio.sfx.empty() &&
              !front.progress->IsCheatUnlocked(kFolder, Cheat::kUnlockDkMode - Cheat::kUnlockPaintball),
          "a stage in between drops the buttons typed before it");
    SetMenuAudio(nullptr);
}

void TestDevCheatsGate() {
    namespace Cheat = Frigate::Game::Menu::Cheat;
    Check(DevCheats::AppliesInStage(Cheat::kAllGuns, 1) && !DevCheats::AppliesInStage(Cheat::kAllGuns, 2),
          "a single player cheat applies only in a solo stage");
    Check(!DevCheats::AppliesInStage(Cheat::kBondPhase, 1) && DevCheats::AppliesInStage(Cheat::kBondPhase, 4),
          "a multiplayer cheat applies only in a multiplayer stage");
    Check(DevCheats::AppliesInStage(Cheat::kLineMode, 1) && DevCheats::AppliesInStage(Cheat::kLineMode, 2),
          "line mode applies in both");
    Check(DevCheats::AppliesInStage(Cheat::kEnemyRockets, 1), "enemy rockets is a stage's cheat");
    Check(!DevCheats::AppliesInStage(Cheat::kExtraMpChars, 1) && !DevCheats::AppliesInStage(Cheat::kUnlockDam, 1),
          "the menus' cheats are not switched in a stage");

    // With no stage, an ask is dropped and nothing reads as on.
    DevCheats::Request(Cheat::kLineMode, true);
    DevCheats::Sync(nullptr);
    const DevCheats::Snapshot none = DevCheats::Current();
    Check(!none.live && !none.active[Cheat::kLineMode], "with no stage nothing is live or on");
}

int main() {
    TestEveryMissionAtEveryDifficultyReachesTheStage();
    TestCompletedMissionRecordsProgress();
    TestStatisticsPreviousGoesToTheMissionList();
    TestDeathAndAbortRecordNothing();
    TestCradleLeadsToCubaAndTheCredits();
    TestStageThatCannotLoadReturnsToTheMissionList();
    TestLegalScreenSkip();
    TestMenuCheatCodes();
    TestDevCheatsGate();
    TestAgentPathToTheStage();
    TestBriefingPagesBeforeTheStage();
    TestOhSevenPathToTheStage();
    TestBackingOutOfEachScreen();
    TestOptionsBackToTheDifficultySelect();
    TestLockedMissionsCannotBeChosen();
    TestCheatsThroughToTheMissions();
    TestMultiplayerNeedsASecondController();
    TestMultiplayerGameReturnsToItsOptions();
    TestMultiplayerStartCarriesCheats();
    TestMultiplayerMenusToTheStageAndBack();
    TestFoldersKeepTheirOwnSelection();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::puts("MenuFlowTest passed");
    return 0;
}
