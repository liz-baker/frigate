#include "game/menu/MpScenarioSelectScreen.h"

#include <memory>
#include <string>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/MpScenarioSelectModel.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBond.h"

namespace Frigate::Game::Menu {

namespace {

constexpr uint32_t kBlack = 0x000000FF;
// A scenario that does not fit the player count (alpha 0x70).
constexpr uint32_t kDimmed = 0x00000070;
constexpr uint32_t kBoxFill = 0x32;

constexpr int32_t kTitleX = 0x37;
constexpr int32_t kTitleY = 0x66;
constexpr int32_t kLabelX = 0x39;
constexpr int32_t kBoxLeft = 0x37;
constexpr int32_t kBoxWidthPad = 0x3C;
constexpr int32_t kBoxTopOffset = -1;
constexpr int32_t kBoxBottomOffset = 0xE;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MpScenarioSelectModel model;
    FolderPaper paper;
};

State& GetState() {
    static State state;
    return state;
}

// init_menu13_mpscenariosel().
void Init(MenuStateMachine&) {
    State& state = GetState();

    state.model.Init(*state.front);
    if (state.resourceManager == nullptr) {
        return;
    }
    state.paper.Load(state.resourceManager, *state.front);
}

// interface_menu13_mpscenario().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();

    const auto next = state.model.Update(Input::ReadPlayer1(), *state.front);
    PlayCues(state.model.Sfx());
    FolderPaper::ShowSwitches(
        *state.front, { WalletSwitch::kTabs, WalletSwitch::kPaper, WalletSwitch::kOhmss, WalletSwitch::kClassified });

    switch (next) {
        case MpScenarioSelectModel::Next::None:
            break;
        case MpScenarioSelectModel::Next::Options:
            menu.ChangeMenu(MenuId::MultiplayerOptions, /*reload=*/false);
            break;
        case MpScenarioSelectModel::Next::Teams:
            menu.ChangeMenu(MenuId::MultiplayerTeams, /*reload=*/false);
            break;
    }
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text, uint32_t colour) {
    return Text::Render(dl, &x, &y, text, state.paper.Zurich(), colour, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// constructor_menu13_mpscenario().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FrontState& front = *state.front;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, front.multiplayer.playerCount);

    // viSetFillColor and viFillScreen: the frame is already cleared to black.
    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);

    dl = DrawText(dl, state, kTitleX, kTitleY, state.paper.String(TitleString::kScenarioSelect), kBlack);

    for (int i = 0; i < static_cast<int>(MpScenario::Count); i++) {
        const uint32_t colour = MpScenarioSelectModel::Fits(static_cast<MpScenario>(i), players) ? kBlack : kDimmed;
        const std::string& text = state.paper.String(TitleString::kFirstScenario + static_cast<size_t>(i));
        const Text::Extent extent = Text::Measure(state.paper.Zurich(), text);
        const int32_t y = MpScenarioSelectModel::kFirstRowTop + (i * MpScenarioSelectModel::kRowSpacing);

        if (i + 1 == state.model.Highlighted()) {
            dl = Text::FillRect(dl, kBoxLeft, y + kBoxTopOffset, extent.width + kBoxWidthPad, y + kBoxBottomOffset,
                                kBoxFill);
        }
        dl = DrawText(dl, state, kLabelX, y, text, colour);
    }

    dl = DrawPreviousTab(dl, state.paper.Gothic(), state.paper.String(TitleString::kPrevious),
                         state.model.TabPrevHighlighted());
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterMpScenarioSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                              const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MultiplayerScenarioSelect, {
                                                             .onInit = Init,
                                                             .onInterface = Interface,
                                                             .onConstruct = Construct,
                                                         });
}

} // namespace Frigate::Game::Menu
