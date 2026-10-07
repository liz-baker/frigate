#include "game/menu/MpCharSelectScreen.h"

#include <array>
#include <deque>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/MultiplayerCharacters.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/MpCharSelectModel.h"
#include "game/menu/WalletBond.h"
#include "gfx/FixedMtx.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "level/EffectsDraw.h"

namespace Frigate::Game::Menu {

namespace {

constexpr uint32_t kBlack = 0x000000FF;
// TITLE_STR_85_SELECTCHARACTER.
constexpr size_t kSelectCharacterString = 85;

// s_mpcharselimages in the global image table: four images a portrait, the
// upper left, upper right, lower left and lower right.
constexpr int32_t kPortraitFirstOffset = 0x102C;
constexpr int32_t kEntrySize = 12;
constexpr int32_t kImagesPerPortrait = 4;

// A panel's frame: the rules across and down the middle, drawn once, and the
// panels' own placement.
constexpr int32_t kRulePanelLeft = 0x26;
constexpr int32_t kRuleHorizontalTop = 0xA9;
constexpr int32_t kRuleHorizontalRight = 0x184;
constexpr int32_t kRuleHorizontalBottom = 0xAB;
constexpr uint32_t kRuleHorizontalFill = 0x90;
constexpr int32_t kRuleVerticalLeft = 0xD4;
constexpr int32_t kRuleVerticalTop = 0x1E;
constexpr int32_t kRuleVerticalRight = 0xD6;
constexpr int32_t kRuleVerticalBottom = 0x136;
constexpr uint32_t kRuleVerticalFill = 0x80;

constexpr int32_t kPanelLeft = 0x26;
constexpr int32_t kPanelTop = 0x1E;
constexpr int32_t kPanelHeight = 0x8C;
constexpr int32_t kWidePanelWidth = 0x15E;
constexpr int32_t kNarrowPanelWidth = 0xAF;
constexpr int32_t kScissorSideInset = 6;
constexpr int32_t kScissorVerticalInset = 5;

constexpr int32_t kPortraitTop = 0x46;
constexpr int32_t kNameTop = 0x32;
constexpr int32_t kPortraitEdgeInset = 0xD;
constexpr int32_t kPortraitFadeInset = 0xE;

// The image's alpha ramp at either end of the strip (frontCalculateCharacterImageAlpha).
constexpr int32_t kFadeWidth = 0x28;

// A quarter image's far edge in s and t: the 64 texels a 65 texel quarter
// shows. The decomp's vertices carry 0x1000, which the interpreter reads as 128
// texels, so the portrait would fill half its quad and clamp the rest.
constexpr int16_t kQuarterTexCoord = 0x800;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MpCharSelectModel model;
    FolderPaper paper;

    // The portraits' vertices and the matrices every portrait loads. The
    // previous frame's vertices stay until the next frame starts, as the
    // interpreter may still be reading them.
    std::array<std::deque<std::array<Vtx_t, 16>>, 2> vertices;
    size_t generation = 0;
    Mtx::FixedMtx ortho{};
    Mtx::FixedMtx rotate{};
};

State& GetState() {
    static State state;
    return state;
}

// init_menu0f_mpcharsel().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    state.model.Init(front);

    if (state.resourceManager == nullptr) {
        return;
    }
    state.paper.Load(state.resourceManager, front);

    // dynAllocateMatrix + guOrtho / guRotate, which every portrait loads
    // alike. guRotate takes degrees, so this tilts the plane by a few
    // degrees rather than flipping it.
    Gu::MatrixF matrix;
    Gu::OrthoF(matrix, 0.0f, 440.0f, 0.0f, 330.0f, 1.0f, 10.0f, 1.0f);
    state.ortho = Mtx::ToFixedMtx(matrix);
    Gu::RotateF(matrix, 3.1415927f, 1.0f, 0.0f, 0.0f);
    state.rotate = Mtx::ToFixedMtx(matrix);
}

// interface_menu0F_mpcharsel().
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

// frontCalculateCharacterImageAlpha(): opaque between the ends, fading out over
// the kFadeWidth before `left` and after `right`.
int32_t PortraitAlpha(int32_t x, int32_t left, int32_t right) {
    if (left >= x || x >= right) {
        return 0;
    }
    if (x < left + kFadeWidth) {
        return 0xF - ((left - x) * 6);
    }
    if (right - kFadeWidth < x) {
        return 0xF - ((x - right) * 6);
    }
    return 0xFF;
}

// frontRenderCharacterPortrait(): character `character`'s four quarter images
// as a 3 by 3 grid of vertices around (`centreX`, `top` from the bottom up),
// growing with `size` once locked in, fading out past `left` and `right`.
Gfx* DrawPortrait(Gfx* dl, State& state, int32_t left, int32_t right, int32_t centreX, int32_t top, int character,
                  int32_t size) {
    using namespace Frigate::F3dex;
    const FrontState& front = *state.front;
    const MultiplayerSettings& settings = front.multiplayer;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);

    auto& store = state.vertices[state.generation];
    store.emplace_back();
    std::array<Vtx_t, 16>& vertices = store.back();

    dl = Text::SetupRdp(dl);

    const bool taken = PlayersHaveSelectedChar(character, players, settings.character, front.playerHasSelectedChar);
    const auto shade = static_cast<uint8_t>(taken && size == 0 ? 0x6E : 0xFF);
    const int32_t halfWidth = size + 0x23;
    const int32_t y = 0x14A - top;
    const int32_t halfHeight = size + 0x2A;

    size_t next = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            const int32_t column = j >= 2 ? j - 1 : j;
            const int32_t row = i >= 2 ? i - 1 : i;

            Vtx_t& vertex = vertices[next++];
            vertex.ob[0] = static_cast<int16_t>(((column - 1) * halfWidth) + centreX);
            vertex.ob[1] = static_cast<int16_t>(y + ((row - 1) * halfHeight) + size);
            vertex.ob[2] = -5;
            vertex.flag = 0;
            vertex.tc[0] = static_cast<int16_t>((j & 1) ? kQuarterTexCoord : 0);
            vertex.tc[1] = static_cast<int16_t>((i & 1) ? kQuarterTexCoord : 0);
            vertex.cn[0] = vertex.cn[1] = vertex.cn[2] = shade;
            vertex.cn[3] = size == 0 ? static_cast<uint8_t>(PortraitAlpha(vertex.ob[0], left, right)) : 0xFF;
        }
    }

    *dl++ = Matrix(&state.ortho, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(&state.rotate, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Vertices(vertices.data(), 16, 0);

    const int32_t photo = kMpCharacters[static_cast<size_t>(character)].photo;
    const int32_t first = kPortraitFirstOffset + (photo * kImagesPerPortrait * kEntrySize);
    auto quad = [&](int32_t image, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        dl = Level::SelectEffectImage(dl, first + (image * kEntrySize), 2, 0, true);
        *dl++ = Triangle(a, b, c);
        *dl++ = Triangle(c, b, d);
    };
    quad(2, 0, 1, 4, 5);
    quad(3, 2, 3, 6, 7);
    quad(0, 8, 9, 12, 13);
    quad(1, 10, 11, 14, 15);
    return dl;
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text) {
    dl = Text::SetupRdp(dl);
    return Text::Render(dl, &x, &y, text, state.paper.Zurich(), kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// constructor_menu0F_mpcharsel().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FrontState& front = *state.front;
    const MpCharSelectModel& model = state.model;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, front.multiplayer.playerCount);

    state.generation ^= 1;
    state.vertices[state.generation].clear();

    // viSetFillColor and viFillScreen: the frame is already cleared to black.
    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);
    dl = Text::FillRect(dl, kRulePanelLeft, kRuleHorizontalTop, kRuleHorizontalRight, kRuleHorizontalBottom,
                        kRuleHorizontalFill);
    if (players >= 3) {
        dl = Text::FillRect(dl, kRuleVerticalLeft, kRuleVerticalTop, kRuleVerticalRight, kRuleVerticalBottom,
                            kRuleVerticalFill);
    }

    for (int player = 0; player < players; player++) {
        int32_t panelLeft;
        int32_t panelTop;
        int32_t panelWidth;
        if (players == 2) {
            panelLeft = kPanelLeft;
            panelWidth = kWidePanelWidth;
            panelTop = (player > 0 ? kPanelHeight : 0) + kPanelTop;
        } else {
            panelWidth = kNarrowPanelWidth;
            panelTop = (player >= 2 ? kPanelHeight : 0) + kPanelTop;
            panelLeft = ((player & 1) != 0 ? kNarrowPanelWidth : 0) + kPanelLeft;
        }

        gDPSetScissor(dl++, G_SC_NON_INTERLACE, panelLeft + kScissorSideInset, panelTop + kScissorVerticalInset,
                      panelLeft + panelWidth - kScissorSideInset, panelTop + kPanelHeight - kScissorVerticalInset);

        const int32_t centreX = (panelWidth >> 1) + panelLeft;
        const auto i = static_cast<size_t>(player);
        const bool choosing = !front.playerHasSelectedChar[i] && model.ImageSize(player) == 0;

        if (choosing) {
            const std::string& title = state.paper.String(kSelectCharacterString);
            const Text::Extent extent = Text::Measure(state.paper.Zurich(), title);
            dl = DrawText(dl, state, centreX - (extent.width >> 1), panelTop + 5, title);
        }

        const std::string& name = state.paper.String(kMpCharacters[static_cast<size_t>(model.Cursor(player))].nameSlot);
        const Text::Extent nameExtent = Text::Measure(state.paper.Zurich(), name);
        const int32_t portraitTop = panelTop + kPortraitTop;
        dl = DrawText(dl, state, centreX - (nameExtent.width >> 1), portraitTop + kNameTop, name);

        const int32_t fadeLeft = panelLeft + kPortraitEdgeInset;
        const int32_t fadeRight = panelLeft + panelWidth - kPortraitFadeInset;
        const int32_t offset = model.ScrollOffset(player);
        const int previous = model.Previous(player);
        const int32_t size = model.ImageSize(player);
        const int32_t stride = MpCharSelectModel::kPortraitStride;

        dl = DrawPortrait(dl, state, fadeLeft, fadeRight, centreX - offset, portraitTop, previous, size);

        if (choosing || previous != model.Cursor(player)) {
            const int count = model.NumCharacters();
            for (int step = 3; step >= 1; step--) {
                if (previous >= step) {
                    dl = DrawPortrait(dl, state, fadeLeft, fadeRight, centreX - offset - (step * stride), portraitTop,
                                      previous - step, size);
                }
            }
            for (int step = 1; step <= 3; step++) {
                if (previous < count - step) {
                    dl = DrawPortrait(dl, state, fadeLeft, fadeRight, centreX - offset + (step * stride), portraitTop,
                                      previous + step, size);
                }
            }
        }
    }

    return Text::RestoreRdp(dl);
}

} // namespace

void RegisterMpCharSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                          const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MultiplayerCharacterSelect, {
                                                              .onInit = Init,
                                                              .onInterface = Interface,
                                                              .onConstruct = Construct,
                                                          });
}

} // namespace Frigate::Game::Menu
