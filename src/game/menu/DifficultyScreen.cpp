#include "game/menu/DifficultyScreen.h"

#include <memory>
#include <string>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/DifficultyModel.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/SaveMissionProgress.h"
#include "game/menu/Options007Screen.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// Text is opaque black on the paper.
constexpr uint32_t kBlack = 0x000000FF;
// The highlight's fill: black at an alpha of 50.
constexpr uint32_t kHighlightFill = 0x32;

// LTITLE string slots.
constexpr size_t kStringDifficulty = 35;
constexpr size_t kStringFirstDifficulty = 36;

// The tick for a completed difficulty (IMG_CHECK in s_mainfolderimages).
constexpr int32_t kTickOffset = 0x1008;
constexpr int32_t kTickSize = 0x14;

// Where the page prints.
constexpr int32_t kLabelX = 0x37;
constexpr int32_t kLabelY = 0x8F;
constexpr int32_t kNumberX = 0x82;
constexpr int32_t kNameX = 0x96;
constexpr int32_t kRowTop = 0xB4;
constexpr int32_t kRowHeight = 0x1E;
constexpr int32_t kHighlightLeft = 0x7E;
constexpr int32_t kHighlightRight = 0xF0;
constexpr int32_t kHighlightTop = 0xB2;
constexpr int32_t kHighlightBottom = 0xC3;
constexpr float kTickX = 280.0f;
constexpr int32_t kTickTop = 0xBA;

// DIFFICULTY_MAX.
constexpr int32_t kDifficultyCount = 4;

// SaveProgress as the model's selected difficulty.
class ProgressSelection final : public DifficultySelection {
  public:
    explicit ProgressSelection(Save::SaveProgress& progress) : progress_(progress) {
    }
    int32_t Get() const override {
        return progress_.SelectedDifficulty();
    }
    // set_selected_difficulty, except for the cleared value.
    void Set(int32_t difficulty) override {
        if (difficulty == Save::kDifficultyMulti) {
            progress_.ClearSelectedDifficulty();
        } else {
            progress_.SetSelectedDifficulty(difficulty);
        }
    }

  private:
    Save::SaveProgress& progress_;
};

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    DifficultyModel model;
    FolderPaper paper;
};

State& GetState() {
    static State state;
    return state;
}

// The mission_folder_setup_entries row of the briefing page, or null.
const MissionFolderSetup* BriefingEntry(const FrontState& front) {
    const auto entries = MissionFolderEntries();
    const int page = front.briefingPage;
    if (page < 0 || static_cast<size_t>(page) >= entries.size()) {
        return nullptr;
    }
    return &entries[static_cast<size_t>(page)];
}

int32_t BriefingLevel(const FrontState& front) {
    const MissionFolderSetup* entry = BriefingEntry(front);
    return entry != nullptr ? entry->missionNum : -1;
}

int32_t UnlockedDifficulty(const FrontState& front) {
    return MissionSelectModel::HighestUnlockedDifficulty(
        BriefingLevel(front), SaveMissionProgress(*front.progress, front.appendCheatSinglePlayer));
}

// init_menu08_difficultyselect().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;
    ProgressSelection selection(*front.progress);
    state.model.Init(selection, front.tabNextSelected, front.tabPrevSelected);
    state.paper.Load(state.resourceManager, front);
}

// interface_menu08_difficulty().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const int32_t unlocked = UnlockedDifficulty(front);
    FolderPaper::ShowSwitches(
        front, { WalletSwitch::kTabs, WalletSwitch::kPaper, WalletSwitch::kOhmss, WalletSwitch::kConfidential });

    ProgressSelection selection(*front.progress);
    const DifficultyModel::Next next =
        state.model.Update(Input::ReadPlayer1(), unlocked, BriefingLevel(front), front.cursor, selection,
                           front.tabNextSelected, front.tabPrevSelected);
    PlayCues(state.model.Sfx());

    switch (next) {
        case DifficultyModel::Next::Options007:
            menu.ChangeMenu(MenuId::Options007, /*reload=*/false);
            break;
        case DifficultyModel::Next::Briefing:
            menu.ChangeMenu(MenuId::Briefing, /*reload=*/false);
            break;
        case DifficultyModel::Next::MissionSelect:
            menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/false);
            break;
        case DifficultyModel::Next::Stay:
            break;
    }
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text) {
    return Text::Render(dl, &x, &y, text, state.paper.Zurich(), kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// The tick beside each completed difficulty.
Gfx* DrawTicks(Gfx* dl, const State& state, int32_t unlocked) {
    const MissionFolderSetup* entry = BriefingEntry(*state.front);
    if (entry == nullptr || entry->missionNum < 0) {
        return dl;
    }
    const Save::SaveProgress& progress = *state.front->progress;
    for (int32_t i = 0; i < 3; i++) {
        if ((i == 0 || unlocked >= i) && progress.IsStageCompleted(progress.CurrentFolder(), entry->missionNum, i)) {
            dl = DrawTableImage(dl, kTickOffset, kTickSize, kTickSize, kTickX,
                                static_cast<float>((i * kRowHeight) + kTickTop), 0xB4, 0, 0, 0xFF);
        }
    }
    return dl;
}

// constructor_menu08_difficulty().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FolderPaper& paper = state.paper;
    const int32_t unlocked = UnlockedDifficulty(*state.front);

    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);
    dl = paper.DrawStageName(dl, *state.front);

    dl = DrawText(dl, state, kLabelX, kLabelY, paper.String(kStringDifficulty));

    const int32_t highlighted = state.model.Highlighted();
    if (highlighted >= 0) {
        dl = Text::FillRect(dl, kHighlightLeft, (highlighted * kRowHeight) + kHighlightTop, kHighlightRight,
                            (highlighted * kRowHeight) + kHighlightBottom, kHighlightFill);
    }

    // Each unlocked difficulty is listed after its number.
    for (int32_t i = 0; i < kDifficultyCount; i++) {
        if (i == 0 || unlocked >= i) {
            // The Japanese version prints its own numbers (j_text_trigger).
            dl = DrawText(dl, state, kNumberX, (i * kRowHeight) + kRowTop, std::to_string(i + 1) + ".\n");
            dl = DrawText(dl, state, kNameX, (i * kRowHeight) + kRowTop,
                          paper.String(kStringFirstDifficulty + static_cast<size_t>(i)));
        }
    }

    dl =
        DrawPreviousTab(dl, paper.Gothic(), paper.String(TitleString::kPrevious), state.model.PreviousTabHighlighted());
    dl = DrawTicks(dl, state, unlocked);
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterDifficulty(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::Difficulty, {
                                              .onInit = Init,
                                              .onInterface = Interface,
                                              .onConstruct = Construct,
                                          });
    RegisterOptions007(menu, resourceManager, front);
}

} // namespace Frigate::Game::Menu
