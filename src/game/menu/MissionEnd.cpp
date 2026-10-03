#include "game/menu/MissionEnd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/resource/type/Texture.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderScreenSpace.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MissionEndModel.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/PageText.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBackground.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "level/Lang.h"
#include "level/ResourceBgSource.h"
#include "level/WeaponTable.h"
#include "model/ModelInstance.h"
#include "resource/type/Font.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kZurichFontPath = "font/fontZurichBold";
constexpr const char* kGothicFontPath = "font/fontBankGothic";

// The Dam row of mission_folder_setup_entries, which the preview reports on.
constexpr int32_t kPreviewBriefingPage = 1;

// SaveProgress as the model's folder interface.
class ProgressSave final : public MissionEndSave {
  public:
    explicit ProgressSave(Save::SaveProgress& progress) : progress_(progress) {
    }
    uint32_t BestTime(int32_t folder, int32_t mission, int32_t difficulty) const override {
        return progress_.BestTime(folder, mission, difficulty);
    }
    void UnlockStage(int32_t folder, int32_t mission, int32_t difficulty, int32_t seconds) override {
        progress_.UnlockStage(folder, mission, difficulty, seconds);
    }
    bool IsCheatUnlocked(int32_t folder, int32_t cheat) const override {
        return progress_.IsCheatUnlocked(folder, cheat);
    }
    void UnlockCheat(int32_t folder, int32_t cheat) override {
        progress_.UnlockCheat(folder, cheat);
    }

  private:
    Save::SaveProgress& progress_;
};

// The loaded banks and font as the model's text interface.
class LoadedText final : public MissionEndText {
  public:
    Level::Lang lang;
    std::shared_ptr<Font> font;
    std::shared_ptr<const Level::WeaponTable> weapons;

    std::string Get(int32_t id) const override {
        return Pages().Get(id);
    }
    Size Measure(std::string_view text) const override {
        return Pages().Measure(text);
    }
    std::string Wrap(int32_t width, std::string_view text, int32_t indent) const override {
        return Pages().Wrap(width, text, indent);
    }
    std::string WeaponOfChoiceName(int32_t item) const override {
        if (weapons == nullptr) {
            return {};
        }
        return Get(static_cast<int32_t>(weapons->Watch(item).longName));
    }

  private:
    FontPageText Pages() const {
        return FontPageText(lang, *font);
    }
};

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MissionFailedModel failed;
    MissionCompleteModel complete;
    MissionContext context;
    MissionOutcome outcome;
    LoadedText text;
    std::shared_ptr<Font> gothic;
    std::optional<WalletBackground> background;
};

State& GetState() {
    static State state;
    return state;
}

// The objectives of a mission's briefing file (load_briefing_text_for_stage's
// ptrbriefingdata); all unassigned when it is not loaded.
std::array<BriefingData::Objective, kObjectivesMax> LoadObjectives(Ship::ResourceManager& resourceManager,
                                                                   const MissionFolderSetup* entry) {
    if (entry == nullptr || entry->briefing == nullptr) {
        return {};
    }
    const auto blob = resourceManager.LoadResource<Ship::Blob>(std::string("obseg/brief/") + entry->briefing);
    const std::optional<BriefingData> data = blob != nullptr ? BriefingData::Parse(blob->Data) : std::nullopt;
    if (!data) {
        spdlog::error("mission end: no briefing file '{}' in the loaded archives", entry->briefing);
        return {};
    }
    return data->objectives;
}

// The selection as the model reads it. Without a current mission row the
// objectives stay empty.
MissionContext BuildContext(const FrontState& front,
                            const std::array<BriefingData::Objective, kObjectivesMax>& objectives) {
    MissionContext context;
    context.briefingPage = front.briefingPage;
    const auto entries = MissionFolderEntries();
    if (front.briefingPage >= 0 && static_cast<size_t>(front.briefingPage) < entries.size()) {
        context.missionNum = entries[static_cast<size_t>(front.briefingPage)].missionNum;
    }
    context.folder = front.progress->CurrentFolder();
    context.difficulty = front.progress->SelectedDifficulty();
    context.cheatsAppended = front.appendCheatSinglePlayer;
    context.objectives = objectives;
    return context;
}

Level::LevelId CurrentStage(const FrontState& front) {
    const MissionFolderSetup* entry = MissionFolderEntryAt(front.briefingPage);
    return entry != nullptr ? entry->stage : kNoLevel;
}

// load_walletbond, load_briefing_text_for_stage and the view the paper is drawn
// through, which both screens' init functions do.
void LoadScreen(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    state.outcome = front.missionOutcome ? *front.missionOutcome : MissionOutcome{};
    if (state.resourceManager == nullptr) {
        state.context = BuildContext(front, {});
        return;
    }

    state.text.font = state.resourceManager->LoadResource<Font>(kZurichFontPath);
    state.gothic = state.resourceManager->LoadResource<Font>(kGothicFontPath);
    if (state.text.font == nullptr || state.gothic == nullptr) {
        spdlog::error("mission end: the fonts are missing from the loaded archives");
    }
    if (!front.walletBond->Load()) {
        spdlog::error("mission end: the wallet model is missing from the loaded archives");
    }

    Level::ResourceBgSource source(state.resourceManager);
    const Level::LevelId stage = CurrentStage(front);
    state.text.lang = Level::Lang{};
    std::vector<int32_t> banks = { Level::TextBank::Gun, Level::TextBank::Title };
    if (const int32_t own = Level::Lang::StageBank(stage); own > 0) {
        banks.push_back(own);
    }
    for (const int32_t bank : banks) {
        if (auto strings = source.LoadTextBank(Level::Lang::BankResource(bank))) {
            state.text.lang.SetBank(bank, std::move(*strings));
        } else {
            spdlog::error("mission end: no text bank {}", Level::Lang::BankResource(bank));
        }
    }
    state.text.weapons = nullptr;
    if (const auto stats = source.LoadWeaponStats()) {
        state.text.weapons = Level::WeaponTable::Parse(*stats);
    }

    state.context =
        BuildContext(front, LoadObjectives(*state.resourceManager, MissionFolderEntryAt(front.briefingPage)));

    state.background.emplace(state.resourceManager);
    state.background->Init();
}

// init_menu0C_missionfailed(). The folders' music restarts when the title
// cycle was running.
void InitFailed(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    LoadScreen(menu);
    state.failed.Init(front.cursor);
    if (front.maybeIsInMenu) {
        // TODO(https://github.com/liz-baker/frigate/issues/242): sndApplyVolumeAllSfxSlot(0x7fff),
        // musicTrack1ApplySeqpVol(0x7fff) and musicTrack1Play(M_FOLDERS).
        front.maybeIsInMenu = false;
    }
}

// init_menu0D_missioncomplete().
void InitComplete(MenuStateMachine& menu) {
    State& state = GetState();
    LoadScreen(menu);
    if (state.complete.Init(state.front->newCheatUnlocked)) {
        // TODO(https://github.com/liz-baker/frigate/issues/242): sndPlaySfx(RARELOGO_SFX).
    }
}

// disable_all_switches and the four switches both screens show.
void SetWalletSwitches(State& state) {
    WalletBond& wallets = *state.front->walletBond;
    if (!wallets.Loaded()) {
        return;
    }
    Model::ModelInstance& model = *wallets.Instance(0);
    DisableAllSwitches(model);
    model.SetSwitchVisible(WalletSwitch::kTabs, true);
    model.SetSwitchVisible(WalletSwitch::kPaper, true);
    model.SetSwitchVisible(WalletSwitch::kOhmss, true);
    model.SetSwitchVisible(WalletSwitch::kClassified, true);
}

// frontAddNextTabText and frontAddPreviousTabText.
Gfx* DrawTabs(Gfx* dl, const State& state, const TabState& tabs) {
    const auto label = [&state](size_t slot) { return state.text.Get(TitleId(static_cast<int32_t>(slot))); };
    dl = DrawNextTab(dl, *state.gothic, label(TitleString::kNext), tabs.nextHighlight);
    return DrawPreviousTab(dl, *state.gothic, label(TitleString::kPrevious), tabs.prevHighlight);
}

Gfx* DrawLines(Gfx* dl, const State& state, const std::vector<TextLine>& lines) {
    for (const TextLine& line : lines) {
        int32_t x = line.x;
        int32_t y = line.y;
        dl = Text::Render(dl, &x, &y, line.text, *state.text.font, line.colour, kMenuScreenWidth, kMenuScreenHeight, 0,
                          0);
    }
    return dl;
}

bool Ready(const State& state) {
    return state.text.font != nullptr && state.gothic != nullptr;
}

// The frame both screens share: the paper, then `lines` and the tabs and the
// cursor over it.
Gfx* ConstructPage(Gfx* dl, State& state, const std::vector<TextLine>& lines, const TabState& tabs) {
    const int32_t folder = std::clamp(state.front->progress->CurrentFolder(), 0, FolderScreenSpace::kFolderCount - 1);
    dl = state.background->Draw(dl, *state.front->walletBond, folder);
    dl = Text::SetupRdp(dl);
    dl = DrawLines(dl, state, lines);
    dl = DrawTabs(dl, state, tabs);
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

// interface_menu0C_missionfailed().
void InterfaceFailed(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    const Input::Sample input = Input::ReadPlayer1();
    SetWalletSwitches(state);
    const MissionFailedModel::Next next = state.failed.Update(input, front.cursor, state.context);
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.failed.Sfx() (sndPlaySfx).
    switch (next) {
        case MissionFailedModel::Next::MissionComplete:
            menu.ChangeMenu(MenuId::MissionComplete, /*reload=*/false);
            break;
        case MissionFailedModel::Next::MissionSelect:
            menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/false);
            break;
        case MissionFailedModel::Next::Stay:
            break;
    }
}

// interface_menu0D_missioncomplete().
void InterfaceComplete(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    const Input::Sample input = Input::ReadPlayer1();
    SetWalletSwitches(state);
    MissionFlow flow{ front.briefingPage, front.selectedStage };
    const MissionCompleteModel::Next next =
        state.complete.Update(input, front.cursor, state.outcome, state.context, flow);
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.complete.Sfx() (sndPlaySfx).
    front.briefingPage = flow.briefingPage;
    front.selectedStage = flow.selectedStage;
    switch (next) {
        case MissionCompleteModel::Next::RunStage:
            menu.ChangeMenu(MenuId::RunStage, /*reload=*/true);
            break;
        case MissionCompleteModel::Next::MissionSelect:
            menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/false);
            break;
        case MissionCompleteModel::Next::Briefing:
            menu.ChangeMenu(MenuId::Briefing, /*reload=*/false);
            break;
        case MissionCompleteModel::Next::Stay:
            break;
    }
}

// constructor_menu0C_missionfailed().
Gfx* ConstructFailed(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!Ready(state)) {
        return dl;
    }
    return ConstructPage(dl, state, MissionEnd::MissionFailedLines(state.text, state.outcome, state.context),
                         state.failed.Tabs());
}

// constructor_menu0D_missioncomplete().
Gfx* ConstructComplete(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!Ready(state)) {
        return dl;
    }
    const uint32_t bestTime =
        state.front->progress->BestTime(state.context.folder, state.context.missionNum, state.context.difficulty);
    return ConstructPage(dl, state,
                         MissionEnd::MissionCompleteLines(state.text, state.outcome, state.context, bestTime,
                                                          state.front->newCheatUnlocked),
                         state.complete.Tabs());
}

} // namespace

void RegisterMissionEnd(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MissionFailed, {
                                                 .onInit = InitFailed,
                                                 .onInterface = InterfaceFailed,
                                                 .onConstruct = ConstructFailed,
                                             });
    menu.RegisterMenu(MenuId::MissionComplete, {
                                                   .onInit = InitComplete,
                                                   .onInterface = InterfaceComplete,
                                                   .onConstruct = ConstructComplete,
                                               });
}

void EndMission(MenuStateMachine& menu, FrontState& front, const MissionOutcome& outcome) {
    State& state = GetState();
    front.missionOutcome = std::make_shared<MissionOutcome>(outcome);

    const Level::LevelId stage = static_cast<Level::LevelId>(front.selectedStage);
    // bossReturnTitleStage: the Cuba stage is the credits, not a mission with
    // a result to record.
    if (stage != Level::LevelId::Cuba && outcome.recordsProgress) {
        const MissionContext context = BuildContext(front, {});
        ProgressSave save(*front.progress);
        front.newCheatUnlocked = MissionEnd::EndOfMissionBriefing(save, outcome, context, front.newCheatUnlocked);
    }

    // interface_menu0B_runstage's tail: the credits cast after Cuba, the
    // report page otherwise.
    if (stage == Level::LevelId::Cuba) {
        front.fullActorIntro = true;
        front.introCharacterIndex = 0;
        menu.ChangeMenu(MenuId::DisplayCast, /*reload=*/true);
    } else {
        menu.ChangeMenu(MenuId::MissionFailed, /*reload=*/true);
    }
}

MenuId PrepareMissionEndPreview(FrontState& front, MissionEndPreview preview) {
    front.briefingPage = kPreviewBriefingPage;
    front.selectedStage = static_cast<int>(Level::LevelId::Dam);
    front.progress->SetCurrentFolder(Save::kFolder1);
    front.progress->SetSelectedDifficulty(Save::kDifficulty00);

    auto outcome = std::make_shared<MissionOutcome>();
    outcome->missionTimer = 134 * 60;
    outcome->killCount = 17;
    outcome->shots = { 120, 6, 18, 4, 1, 0, 2 };
    outcome->favouriteWeapon = { 2, 2 };
    outcome->objectives.fill(Level::ObjectiveStatus::Complete);
    switch (preview) {
        case MissionEndPreview::Failed:
            outcome->objectives[1] = Level::ObjectiveStatus::Failed;
            break;
        case MissionEndPreview::KilledInAction:
            outcome->objectives[2] = Level::ObjectiveStatus::Incomplete;
            outcome->bondKilledInAction = true;
            break;
        case MissionEndPreview::Aborted:
            outcome->objectives[2] = Level::ObjectiveStatus::Incomplete;
            outcome->missionFailedOrAborted = true;
            break;
        case MissionEndPreview::Completed:
        case MissionEndPreview::Statistics:
            break;
    }
    front.missionOutcome = outcome;
    return preview == MissionEndPreview::Statistics ? MenuId::MissionComplete : MenuId::MissionFailed;
}

} // namespace Frigate::Game::Menu
