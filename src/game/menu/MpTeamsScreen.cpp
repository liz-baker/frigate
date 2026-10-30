#include "game/menu/MpTeamsScreen.h"

#include <memory>
#include <string>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/MpPlayerPanels.h"
#include "game/menu/MpTeamsModel.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBond.h"

namespace Frigate::Game::Menu {

namespace {

constexpr uint32_t kBlack = 0x000000FF;
constexpr uint32_t kRed = 0xFF0000FF;
constexpr uint32_t kBlue = 0x0000FFFF;

// A panel's place in the grid (always two columns, whatever the player count)
// and its scissor.
constexpr int32_t kColumnWidth = 0xAF;
constexpr int32_t kRowHeight = 0x8C;
constexpr int32_t kFirstRowTop = 0x1E;
constexpr int32_t kScissorLeft = 0x2C;
constexpr int32_t kScissorRight = 0xCF;
constexpr int32_t kScissorTopInset = 5;
constexpr int32_t kScissorBottomInset = 0x87;
constexpr int32_t kCentreX = 0x7D;
constexpr int32_t kTeamTextCentre = 0x46;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MpTeamsModel model;
    FolderPaper paper;
};

State& GetState() {
    static State state;
    return state;
}

// init_menu14_mpteamsel().
void Init(MenuStateMachine&) {
    State& state = GetState();

    state.model.Init(*state.front);
    if (state.resourceManager == nullptr) {
        return;
    }
    state.paper.Load(state.resourceManager, *state.front);
}

// interface_menu14_mpteams().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const bool accepted = state.model.Update(Input::ReadPlayer(0), front);
    PlayCues(state.model.Sfx());
    FolderPaper::ShowSwitches(front, { WalletSwitch::kTabs, WalletSwitch::kBlank });

    if (accepted) {
        menu.SwitchMenu(MenuId::MultiplayerOptions, /*reload=*/false);
    }
}

Gfx* DrawText(Gfx* dl, const FolderPaper& paper, int32_t x, int32_t y, const std::string& text, uint32_t colour) {
    dl = Text::SetupRdp(dl);
    return Text::Render(dl, &x, &y, text, paper.Zurich(), colour, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// constructor_menu14_mpteams().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FrontState& front = *state.front;
    const FolderPaper& paper = state.paper;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, front.multiplayer.playerCount);

    // viSetFillColor and viFillScreen: the frame is already cleared to black.
    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);
    dl = DrawPanelRules(dl, players);

    for (int player = 0; player < players; player++) {
        const int32_t rowTop = (player >= 2 ? kRowHeight : 0) + kFirstRowTop;
        const int32_t columnLeft = (player & 1) != 0 ? kColumnWidth : 0;

        gDPSetScissor(dl++, G_SC_NON_INTERLACE, columnLeft + kScissorLeft, rowTop + kScissorTopInset,
                      columnLeft + kScissorRight, rowTop + kScissorBottomInset);

        const int32_t centreX = columnLeft + kCentreX;

        if (player == 0) {
            const std::string& title = paper.String(TitleString::kSelectTeams);
            const Text::Extent extent = Text::Measure(paper.Zurich(), title);
            dl = DrawText(dl, paper, centreX - (extent.width >> 1), rowTop + kScissorTopInset, title, kBlack);
        }

        const bool firstTeam = front.multiplayer.TeamFlag(player) == 0;
        const std::string& text = paper.String(firstTeam ? TitleString::kRedTeam : TitleString::kBlueTeam);
        const Text::Extent extent = Text::Measure(paper.Zurich(), text);

        if (state.model.TextShown(front, player)) {
            dl = DrawText(dl, paper, centreX - (extent.width >> 1), (rowTop - (extent.height >> 1)) + kTeamTextCentre,
                          text, firstTeam ? kRed : kBlue);
        }
    }

    return Text::RestoreRdp(dl);
}

} // namespace

void RegisterMpTeams(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                     const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MultiplayerTeams, {
                                                    .onInit = Init,
                                                    .onInterface = Interface,
                                                    .onConstruct = Construct,
                                                });
}

} // namespace Frigate::Game::Menu
