#include "game/menu/MpPlayerPanels.h"

#include <string>

#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderPaper.h"

namespace Frigate::Game::Menu {

namespace {

constexpr uint32_t kBlack = 0x000000FF;

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

// The text's centre line below the panel's top, and how far the heading sits
// above it and the value below.
constexpr int32_t kCentreLine = 0x46;
constexpr int32_t kTextSpread = 0xF;

Gfx* DrawCentredText(Gfx* dl, const FolderPaper& paper, int32_t centreX, int32_t centreY, const std::string& text) {
    const Text::Extent extent = Text::Measure(paper.Zurich(), text);
    int32_t x = centreX - (extent.width >> 1);
    int32_t y = centreY - (extent.height >> 1);
    dl = Text::SetupRdp(dl);
    return Text::Render(dl, &x, &y, text, paper.Zurich(), kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

} // namespace

Gfx* DrawPanelRules(Gfx* dl, int players) {
    dl = Text::FillRect(dl, kRulePanelLeft, kRuleHorizontalTop, kRuleHorizontalRight, kRuleHorizontalBottom,
                        kRuleHorizontalFill);
    if (players >= 3) {
        dl = Text::FillRect(dl, kRuleVerticalLeft, kRuleVerticalTop, kRuleVerticalRight, kRuleVerticalBottom,
                            kRuleVerticalFill);
    }
    return dl;
}

Gfx* DrawChoicePanel(Gfx* dl, const FolderPaper& paper, int player, int players, bool locked, size_t headingSlot,
                     size_t valueSlot) {
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
    const int32_t centreY = panelTop + kCentreLine;
    if (!locked) {
        dl = DrawCentredText(dl, paper, centreX, centreY - kTextSpread, paper.String(headingSlot));
    }
    return DrawCentredText(dl, paper, centreX, centreY + kTextSpread, paper.String(valueSlot));
}

} // namespace Frigate::Game::Menu
