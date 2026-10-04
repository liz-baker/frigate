#include "game/menu/FolderTabs.h"

#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "libultraship/libultra/gbi.h"
#include "resource/type/Font.h"

namespace Frigate::Game::Menu {

namespace {

// The fill behind a highlighted tab: black at an alpha of 50.
constexpr uint32_t kHighlightFill = 50;

Gfx* DrawTab(Gfx* dl, const Font& font, std::string_view text, int32_t textTop, int32_t textBottom, bool highlighted) {
    Text::SetSpacingInverted(true);
    // The text runs down the tab, so its measured height is the tab's width
    // and its measured width the length along it.
    const Text::Extent extent = Text::Measure(font, text);
    int32_t across = FolderTab::kRightEdge - (extent.height / 2);

    if (highlighted) {
        dl = Text::FillRect(dl, (across - extent.height) + 1, textTop, across, textBottom, kHighlightFill);
    }

    Text::SetOrientation(Text::Orientation::kRotated90Clockwise);
    const int32_t textHalf = textTop + ((textBottom - textTop) / 2);
    int32_t along = textHalf - (extent.width / 2);
    dl = Text::Render(dl, &along, &across, text, font, 0x000000FF, kMenuScreenHeight, kMenuScreenWidth, 0, 0);
    Text::SetOrientation(Text::Orientation::kNormal);
    Text::SetSpacingInverted(false);
    return dl;
}

} // namespace

Gfx* DrawStartTab(Gfx* dl, const Font& font, std::string_view text, bool highlighted) {
    return DrawTab(dl, font, text, FolderTab::kStartTextTop, FolderTab::kStartTextBottom, highlighted);
}

Gfx* DrawNextTab(Gfx* dl, const Font& font, std::string_view text, bool highlighted) {
    return DrawTab(dl, font, text, FolderTab::kNextTextTop, FolderTab::kNextTextBottom, highlighted);
}

Gfx* DrawPreviousTab(Gfx* dl, const Font& font, std::string_view text, bool highlighted) {
    return DrawTab(dl, font, text, FolderTab::kPreviousTextTop, FolderTab::kPreviousTextBottom, highlighted);
}

} // namespace Frigate::Game::Menu
