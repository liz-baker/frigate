#include "game/menu/Options007Screen.h"

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
#include "game/menu/TitleStrings.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/Options007Model.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr uint32_t kBlack = 0x000000FF;
// The fills: the bar's track and a highlighted label (black at an alpha of
// 50), and the bar's filled part (100).
constexpr uint32_t kTrackFill = 0x00000032;
constexpr uint32_t kBarFill = 0x00000064;

// LTITLE string slots.
constexpr size_t kStringSpecialOptions = 40;
constexpr size_t kStringReaction = 41;
constexpr size_t kStringHealth = 42;
constexpr size_t kStringDamage = 43;
constexpr size_t kStringAccuracy = 44;

constexpr int32_t kTitleX = 55;
constexpr int32_t kTitleY = 143;
constexpr int32_t kLabelX = 57;
constexpr int32_t kTrackRight = 355;
constexpr int32_t kHighlightRight = 199;
constexpr int32_t kPercentRight = 285;

// The label's y of each slider's row; its track sits 17 below, 11 high.
constexpr int32_t kHealthRow = 164;
constexpr int32_t kDamageRow = 197;
constexpr int32_t kAccuracyRow = 230;
constexpr int32_t kReactionRow = 263;
constexpr int32_t kTrackOffset = 17;
constexpr int32_t kTrackHeight = 11;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    Options007Model model;
    FolderPaper paper;
};

State& GetState() {
    static State state;
    return state;
}

// init_menu09_007difficultyselect().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;
    state.model.Init(front.tabNextSelected, front.tabPrevSelected);
    state.paper.Load(state.resourceManager, front);
}

// interface_menu09_007options().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const Options007Model::Next next = state.model.Update(Input::ReadPlayer1(), front.cursor, front.sliders007,
                                                          front.tabNextSelected, front.tabPrevSelected);
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.model.Sfx() (sndPlaySfx).
    FolderPaper::ShowSwitches(
        front, { WalletSwitch::kTabs, WalletSwitch::kPaper, WalletSwitch::kOhmss, WalletSwitch::kClassified });

    switch (next) {
        case Options007Model::Next::RunStage:
            // TODO(https://github.com/liz-baker/frigate/issues/295): the hand-off to the stage; until then the
            // run-stage screen returns to the legal screen.
            menu.ChangeMenu(MenuId::RunStage, /*reload=*/true);
            break;
        case Options007Model::Next::Briefing:
            // TODO(https://github.com/liz-baker/frigate/issues/295): the briefing's handlers; until then the
            // screen it lands on draws only the black clear.
            menu.ChangeMenu(MenuId::Briefing, /*reload=*/false);
            break;
        case Options007Model::Next::Difficulty:
            menu.ChangeMenu(MenuId::Difficulty, /*reload=*/false);
            SetCursorForDifficulty(front.cursor, front.progress->SelectedDifficulty());
            break;
        case Options007Model::Next::Stay:
            break;
    }
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text) {
    return Text::Render(dl, &x, &y, text, state.paper.Zurich(), kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// One slider: its track, the filled part up to `barWidth`, the dark box under a
// highlighted label, the label and the percentage right-aligned at x 285.
Gfx* DrawSlider(Gfx* dl, const State& state, int32_t labelY, size_t labelSlot, bool highlighted, int32_t barWidth,
                int32_t percent) {
    const int32_t trackTop = labelY + kTrackOffset;
    dl = Text::FillRect(dl, kTitleX, trackTop, kTrackRight, trackTop + kTrackHeight, kTrackFill);
    dl = Text::FillRect(dl, kTitleX, trackTop, barWidth + kSliderLeft, trackTop + kTrackHeight, kBarFill);
    if (highlighted) {
        dl = Text::FillRect(dl, kTitleX, labelY - 1, kHighlightRight, labelY + 14, kTrackFill);
    }
    dl = DrawText(dl, state, kLabelX, labelY, state.paper.String(labelSlot));

    const std::string text = std::to_string(percent) + "%\n";
    const Text::Extent size = Text::Measure(state.paper.Zurich(), text);
    return DrawText(dl, state, kPercentRight - size.width, labelY, text);
}

// constructor_menu09_007options().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FolderPaper& paper = state.paper;
    const Sliders007& sliders = state.front->sliders007;
    const Options007Model& model = state.model;

    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);
    dl = paper.DrawStageName(dl, *state.front);

    dl = DrawText(dl, state, kTitleX, kTitleY, paper.String(kStringSpecialOptions));

    dl = DrawSlider(dl, state, kHealthRow, kStringHealth, model.HealthHighlighted(), SquaredBarWidth(sliders.health),
                    SliderPercent(sliders.health));
    dl = DrawSlider(dl, state, kDamageRow, kStringDamage, model.DamageHighlighted(), SquaredBarWidth(sliders.damage),
                    SliderPercent(sliders.damage));
    dl = DrawSlider(dl, state, kAccuracyRow, kStringAccuracy, model.AccuracyHighlighted(),
                    SquaredBarWidth(sliders.accuracy), AccuracyPercent(sliders.accuracy));
    dl = DrawSlider(dl, state, kReactionRow, kStringReaction, model.ReactionHighlighted(),
                    ReactionBarWidth(sliders.reaction), SliderPercent(sliders.reaction));

    dl = DrawStartTab(dl, paper.Gothic(), paper.String(TitleString::kStart), model.TabStartHighlighted());
    dl = DrawPreviousTab(dl, paper.Gothic(), paper.String(TitleString::kPrevious), model.TabPrevHighlighted());
    dl = DrawNextTab(dl, paper.Gothic(), paper.String(TitleString::kNext), model.TabNextHighlighted());
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterOptions007(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::Options007, {
                                              .onInit = Init,
                                              .onInterface = Interface,
                                              .onConstruct = Construct,
                                          });
}

} // namespace Frigate::Game::Menu
