#include "game/menu/MpStageSelectScreen.h"

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
#include "game/menu/MpStageSelectLayout.h"
#include "game/menu/MpStageSelectModel.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBond.h"
#include "level/EffectsDraw.h"
#include "level/ImageDraw.h"

namespace Frigate::Game::Menu {

namespace {

using namespace MpStageSelectLayout;

constexpr uint32_t kRuleColour = 0x101010FF;

// The global image table's IMG_DOT, which tiles into each rule's pattern, and
// s_mpstageselimages (docs/formats/goldeneye-hud-images.md).
constexpr int32_t kDotOffset = 0x1014;
constexpr int32_t kPreviewFirstOffset = 0x132C;
constexpr int32_t kEntrySize = 12;
constexpr int32_t kPreviewWidth = 0x44;
constexpr int32_t kPreviewHeight = 0x2C;

constexpr int32_t kDotTextureWidth = 0x2F0;
constexpr int32_t kDotTextureHeight = 0x12;
constexpr float kDotHalfWidth = 176.0f;

constexpr uint32_t kCaptionColour = 0x96969600;
constexpr uint32_t kCaptionHighlightColour = 0xFFFFFF00;
constexpr uint32_t kCaptionGlowAlpha = 0x64;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MpStageSelectModel model;
    FolderPaper paper;
};

State& GetState() {
    static State state;
    return state;
}

// init_menu12_mpstage().
void Init(MenuStateMachine&) {
    State& state = GetState();

    state.model.Init(*state.front);
    if (state.resourceManager == nullptr) {
        return;
    }
    state.paper.Load(state.resourceManager, *state.front);
}

// interface_menu12_mpstage().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();

    const bool done = state.model.Update(Input::ReadPlayer1(), *state.front);
    PlayCues(state.model.Sfx());
    FolderPaper::ShowSwitches(*state.front, { WalletSwitch::kTabs, WalletSwitch::kBlank, WalletSwitch::kOhmss });

    if (done) {
        menu.ChangeMenu(MenuId::MultiplayerOptions, /*reload=*/false);
    }
}

// texSelect for an image of the global image table with the half-texel sample
// offset, and for the highlighted preview the two cycle blend with white fog.
Gfx* SelectImage(Gfx* dl, int32_t offset, bool highlight) {
    dl = Level::SelectEffectImage(dl, offset, 1, 0, true);
    if (highlight) {
        gDPSetCycleType(dl++, G_CYC_2CYCLE);
        gDPSetFogColor(dl++, 0xFF, 0xFF, 0xFF, 0x0A);
        gDPSetRenderMode(dl++, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
    }
    return dl;
}

// constructor_menu12_mpstage().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FrontState& front = *state.front;
    const MpStageSelectModel& model = state.model;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, front.multiplayer.playerCount);
    const auto unlocked = MultiplayerUnlocks::UnlockedStages(*front.progress, players);

    // viSetFillColor and viFillScreen: the frame is already cleared to black.
    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);
    for (int i = 0; i < 3; i++) {
        dl = Text::FillRect(dl, kRuleLeft, kRuleTop + (i * kRuleSpacing), kRuleRight, kRuleBottom + (i * kRuleSpacing),
                            kRuleColour);
    }

    dl = Text::RestoreRdp(dl);
    gDPSetTextureFilter(dl++, G_TF_POINT);

    // The dotted pattern of each pair of rules.
    const float dotHalf[2] = { kDotHalfWidth, kDotHalfHeight };
    float dotPosition[2] = { kDotHalfWidth + 37.0f, kDotFirstY };
    dl = SelectImage(dl, kDotOffset, false);
    for (int i = 0; i < 3; i++) {
        for (int rule = 0; rule < 2; rule++) {
            const float position[2] = { dotPosition[0], dotPosition[1] + (rule * kDotPairSpacing) };
            dl = Level::DisplayImageAtPosition(dl, position, dotHalf, kDotTextureWidth, kDotTextureHeight, false, false,
                                               true, 0x6B, 0x67, 0x53, 0xFF);
        }
        dotPosition[1] += kDotRowSpacing;
    }

    const float previewHalf[2] = { kPreviewHalfWidth, kPreviewHalfHeight };
    float previewPosition[2];
    previewPosition[1] = kPreviewFirstY;
    int stage = 0;
    for (int row = 0; row < MpStageSelectModel::kRows; row++) {
        previewPosition[0] = kPreviewFirstX;
        for (int column = 0; column < MpStageSelectModel::kColumns; column++) {
            const int32_t offset = kPreviewFirstOffset + (MpStageSelectModel::kPhoto[stage] * kEntrySize);
            const bool highlighted = stage == model.Highlighted();
            const uint8_t tint = PreviewTint(highlighted, unlocked[static_cast<size_t>(stage)]);
            dl = SelectImage(dl, offset, highlighted);
            dl = Level::DisplayImageAtPosition(dl, previewPosition, previewHalf, kPreviewWidth, kPreviewHeight, false,
                                               false, true, tint, tint, tint, 0xFF, highlighted);
            previewPosition[0] += kPreviewColumnSpacing;
            stage++;
        }
        previewPosition[1] += kPreviewRowSpacing;
    }

    // The open stages' captions, bottom aligned under their previews: each is
    // drawn over a clear box, then again at a low alpha as a glow.
    dl = Text::SetupRdp(dl);
    stage = 0;
    int32_t bottom = kCaptionFirstBottom;
    for (int row = 0; row < MpStageSelectModel::kRows; row++) {
        int32_t centre = kCaptionFirstX;
        for (int column = 0; column < MpStageSelectModel::kColumns; column++) {
            if (unlocked[static_cast<size_t>(stage)]) {
                const uint32_t colour = stage == model.Highlighted() ? kCaptionHighlightColour : kCaptionColour;
                const std::string& caption = state.paper.String(TitleString::kMpStageSelectText[stage]);
                const Text::Extent extent = Text::Measure(state.paper.Gothic(), caption);

                int32_t x = centre - kCaptionLeftInset;
                int32_t y = bottom - extent.height;
                dl = Text::FillRect(dl, x, y, x + extent.width, y + extent.height, 0);
                dl = Text::Render(dl, &x, &y, caption, state.paper.Gothic(), colour | 0xFF, kMenuScreenWidth,
                                  kMenuScreenHeight, 0, 0);

                x = centre - kCaptionLeftInset;
                y = bottom - extent.height;
                dl = Text::Render(dl, &x, &y, caption, state.paper.Gothic(), colour | kCaptionGlowAlpha,
                                  kMenuScreenWidth, kMenuScreenHeight, 0, 0);
            }
            centre += kCaptionColumnSpacing;
            stage++;
        }
        bottom += kCaptionRowSpacing;
    }

    dl = Text::SetupRdp(dl);
    dl = DrawPreviousTab(dl, state.paper.Gothic(), state.paper.String(TitleString::kPrevious),
                         model.TabPrevHighlighted());
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterMpStageSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MultiplayerStageSelect, {
                                                          .onInit = Init,
                                                          .onInterface = Interface,
                                                          .onConstruct = Construct,
                                                      });
}

} // namespace Frigate::Game::Menu
