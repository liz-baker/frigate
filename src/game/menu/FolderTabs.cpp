#include "game/menu/FolderTabs.h"

#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderTabLayout.h"

namespace Frigate::Game::Menu {

namespace {

// The label's black, opaque colour and the box's fill.
constexpr uint32_t kLabelColour = 0x000000FF;
constexpr uint32_t kHighlightFill = 50;

} // namespace

Gfx* DrawPreviousTab(Gfx* dl, const Font& gothic, std::string_view label, bool highlighted) {
    Text::SetSpacingInverted(true);
    // The text runs down the tab, so the decomp takes the measured height as
    // the tab's width and the measured width as its length.
    const Text::Extent extent = Text::Measure(gothic, label);
    const int32_t textWidth = extent.height;
    const int32_t textHeight = extent.width;
    int32_t h = kTabsRightEdge - (textWidth / 2);

    if (highlighted) {
        dl = Text::FillRect(dl, (h - textWidth) + 1, kPreviousTabTextTop, h, kPreviousTabTextBottom, kHighlightFill);
    }

    Text::SetOrientation(Text::Orientation::kRotated90Clockwise);
    int32_t v = kPreviousTabTextHalf - (textHeight / 2);
    // textRender(&v, &h, ..., viGetY(), viGetX()): the clip extent is swapped
    // along with the coordinates.
    dl = Text::Render(dl, &v, &h, label, gothic, kLabelColour, kMenuScreenHeight, kMenuScreenWidth, 0, 0);
    Text::SetOrientation(Text::Orientation::kNormal);
    Text::SetSpacingInverted(false);
    return dl;
}

} // namespace Frigate::Game::Menu
