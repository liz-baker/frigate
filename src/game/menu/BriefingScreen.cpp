#include "game/menu/BriefingScreen.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/BriefingModel.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "level/Lang.h"
#include "model/ModelInstance.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kBriefingPath = "obseg/brief/";

// Where the text sits: the page's heading and body, the objectives' letters
// and their text (the stage name above them is FolderPaper's).
constexpr int32_t kTextLeft = 0x37;
constexpr int32_t kHeadingY = 0x8F;
constexpr int32_t kBodyTop = 0xA7;
constexpr int32_t kObjectiveTextLeft = 0x4B;
constexpr int32_t kBodyWrapWidth = 0x140;
constexpr int32_t kOverlapCorrection = 8;

constexpr uint32_t kBlack = 0x000000FF;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    FolderPaper paper;
    BriefingModel model;

    // The title bank and the briefed stage's own bank (langLoadToBank).
    Level::Lang lang;
    int32_t stageBank = -1;
    std::optional<BriefingData> data;
    // briefingpage is a row of a stage; otherwise there is nothing to brief.
    bool valid = false;
};

State& GetState() {
    static State state;
    return state;
}

const MissionFolderSetup* Entry(const State& state) {
    const std::span<const MissionFolderSetup> entries = MissionFolderEntries();
    const int32_t page = state.front->briefingPage;
    if (page < 0 || static_cast<size_t>(page) >= entries.size() ||
        entries[static_cast<size_t>(page)].label == nullptr || entries[static_cast<size_t>(page)].briefing == nullptr) {
        return nullptr;
    }
    return &entries[static_cast<size_t>(page)];
}

bool LoadBank(State& state, int32_t bank) {
    auto text = state.resourceManager->LoadResource<Frigate::Text>(Level::Lang::BankResource(bank));
    if (text == nullptr) {
        return false;
    }
    state.lang.SetBank(bank, text->Strings);
    return true;
}

// load_briefing_text_for_stage: the briefing file and the stage's text.
void LoadBriefingText(State& state, const MissionFolderSetup& entry) {
    state.data.reset();
    auto blob = state.resourceManager->LoadResource<Ship::Blob>(std::string(kBriefingPath) + entry.briefing);
    if (blob != nullptr) {
        state.data = BriefingData::Parse(blob->Data);
    }
    state.stageBank = Level::Lang::StageBank(entry.stage);
    if (!state.data || !LoadBank(state, Level::TextBank::Title) || !LoadBank(state, state.stageBank)) {
        spdlog::error("briefing: the briefing file or text banks for '{}' are missing from the loaded archives",
                      entry.briefing);
        state.valid = false;
    }
}

// init_menu0A_briefing().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    state.model.Init();
    state.paper.Load(state.resourceManager, front);

    const MissionFolderSetup* entry = Entry(state);
    state.valid = entry != nullptr;
    if (entry == nullptr) {
        spdlog::error("briefing: no stage is selected to brief (page {})", front.briefingPage);
        return;
    }
    LoadBriefingText(state, *entry);
}

// update_menu0A_briefing(): the stage's text bank and the wallets are let go.
void Update(const MenuStateMachine&) {
    State& state = GetState();
    state.lang = Level::Lang();
    state.data.reset();
    state.paper.Release();
    state.front->walletBond->CleanUp();
}

// The wallet's pages for this frame, after the input has moved them.
void SetWalletSwitches(State& state, const MissionFolderSetup& entry) {
    WalletBond& wallets = *state.front->walletBond;
    if (!wallets.Loaded()) {
        return;
    }
    Model::ModelInstance& model = *wallets.Instance(0);
    const bool onTitle = state.model.OnTitlePage();
    DisableAllSwitches(model);
    model.SetSwitchVisible(WalletSwitch::kPhotoBrief, onTitle);
    model.SetSwitchVisible(WalletSwitch::kBrief1 + static_cast<size_t>(entry.missionNum), onTitle);
    model.SetSwitchVisible(WalletSwitch::kTabs, true);
    model.SetSwitchVisible(WalletSwitch::kPaper, true);
    model.SetSwitchVisible(WalletSwitch::kOhmss, true);
    model.SetSwitchVisible(WalletSwitch::kClassified, true);
}

// interface_menu0A_briefing().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    const MissionFolderSetup* entry = Entry(state);
    if (!state.valid || entry == nullptr) {
        menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/false);
        return;
    }

    const Input::Sample input = Input::ReadPlayer1();
    const BriefingModel::Next next = state.model.Update(input, front.cursor, entry->missionNum);
    front.tabNextSelected = state.model.TabNextSelected();
    front.tabPrevSelected = state.model.TabPrevSelected();
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.model.Sfx() (sndPlaySfx).
    SetWalletSwitches(state, *entry);

    switch (next) {
        case BriefingModel::Next::RunStage:
            front.selectedStage = static_cast<int32_t>(entry->stage);
            menu.ChangeMenu(MenuId::RunStage, /*reload=*/true);
            break;
        case BriefingModel::Next::MissionSelect:
            menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/false);
            break;
        case BriefingModel::Next::Stay:
            break;
    }
}

Gfx* DrawText(Gfx* dl, const Font& font, int32_t x, int32_t y, const std::string& text) {
    return Text::Render(dl, &x, &y, text, font, kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// print_objectives_and_status_to_menu for the objectives page: each objective
// shown at the selected difficulty, lettered, with its text wrapped beside it.
Gfx* DrawObjectives(Gfx* dl, const State& state) {
    const Font& font = state.paper.Zurich();
    const int32_t lineHeight = Text::Measure(font, "a.\n").height;
    int32_t lines = 0;
    char letter = 'a';
    for (const uint16_t textId : state.data->ObjectivesAt(state.front->progress->SelectedDifficulty())) {
        const int32_t y = (lineHeight * lines) + kBodyTop;
        dl = DrawText(dl, font, kTextLeft, y, std::string(1, letter++) + ".\n");

        const std::string wrapped = Text::Wrap(kBodyWrapWidth, state.lang.Get(textId), font);
        dl = DrawText(dl, font, kObjectiveTextLeft, y, wrapped);
        for (const char c : wrapped) {
            if (c == '\n') {
                lines++;
            }
        }
    }
    return dl;
}

// constructor_menu0A_briefing().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    const MissionFolderSetup* entry = Entry(state);
    if (!state.valid || entry == nullptr || !state.paper.Ready()) {
        return dl;
    }

    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);
    dl = state.paper.DrawStageName(dl, *state.front);

    const BriefingPage page = state.model.Page();
    dl = DrawText(dl, state.paper.Zurich(), kTextLeft, kHeadingY, std::string(PageHeading(state.lang, page)));
    if (state.model.OnTitlePage()) {
        dl = DrawObjectives(dl, state);
    } else {
        const std::string body =
            Text::Wrap(kBodyWrapWidth, PageBody(state.lang, *state.data, page), state.paper.Zurich());
        Text::SetOverlapCorrection(kOverlapCorrection);
        dl = DrawText(dl, state.paper.Zurich(), kTextLeft, kBodyTop, body);
        Text::SetOverlapCorrection(-1);
    }

    const BriefingTabs& tabs = state.model.Tabs();
    const FolderPaper& paper = state.paper;
    dl = DrawStartTab(dl, paper.Gothic(), paper.String(TitleString::kStart), tabs.start);
    dl = DrawPreviousTab(dl, paper.Gothic(), paper.String(TitleString::kPrevious), tabs.previous);
    if (state.model.ShowsNextTab()) {
        dl = DrawNextTab(dl, paper.Gothic(), paper.String(TitleString::kNext), tabs.next);
    }
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterBriefing(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                      const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::Briefing, {
                                            .onUpdate = Update,
                                            .onInit = Init,
                                            .onInterface = Interface,
                                            .onConstruct = Construct,
                                        });
}

} // namespace Frigate::Game::Menu
