#include "game/menu/MpControlStyleScreen.h"

#include <array>
#include <memory>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/TextRender.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/MpControlStyleModel.h"
#include "game/menu/MpPlayerPanels.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBond.h"

namespace Frigate::Game::Menu {

namespace {

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MpControlStyleModel model;
    FolderPaper paper;
};

State& GetState() {
    static State state;
    return state;
}

// init_menu11_mpcontrol().
void Init(MenuStateMachine&) {
    State& state = GetState();

    state.model.Init(*state.front);
    if (state.resourceManager == nullptr) {
        return;
    }
    state.paper.Load(state.resourceManager, *state.front);
}

// interface_menu11_mpcontrols().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    std::array<Input::Sample, kMpMaxPlayers> inputs;
    for (int port = 0; port < kMpMaxPlayers; port++) {
        inputs[static_cast<size_t>(port)] = Input::ReadPlayer(port);
    }
    const bool allReady = state.model.Update(inputs, front);
    PlayCues(state.model.Sfx());
    FolderPaper::ShowSwitches(front, { WalletSwitch::kTabs, WalletSwitch::kBlank });

    if (allReady) {
        menu.ChangeMenu(MenuId::MultiplayerOptions, /*reload=*/false);
    }
}

// constructor_menu11_mpcontrol().
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
    dl = DrawPanelRules(dl, players);

    for (int player = 0; player < players; player++) {
        const auto i = static_cast<size_t>(player);
        dl = DrawChoicePanel(dl, state.paper, player, players, front.playerHasSelectedChar[i],
                             TitleString::kSelectControlStyle,
                             TitleString::kFirstControlStyle + static_cast<size_t>(front.multiplayer.controlStyle[i]));
    }

    return Text::RestoreRdp(dl);
}

} // namespace

void RegisterMpControlStyle(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                            const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MultiplayerControlStyle, {
                                                           .onInit = Init,
                                                           .onInterface = Interface,
                                                           .onConstruct = Construct,
                                                       });
}

} // namespace Frigate::Game::Menu
