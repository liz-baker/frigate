#include "level/MpWatchMenuDraw.h"

#include "game/TextRender.h"

namespace Frigate::Level {

namespace Text = Frigate::Game::Text;

Gfx* DrawMpWatchPlan(Gfx* dl, const MpWatchPlan& plan, const Font& font, const ViewMetrics& screen,
                     const PlayerView& view) {
    if (!plan.menuOpen && !plan.continuePrompt) {
        return dl;
    }
    if (plan.continuePrompt) {
        const Scissor area = ViewScissor(view);
        gDPSetScissor(dl++, G_SC_NON_INTERLACE, area.left, area.top, area.right, area.bottom);
    }
    dl = Text::SetupRdp(dl);
    for (const MpMenuText& line : plan.texts) {
        int32_t x = line.x;
        int32_t y = line.y;
        if (line.outlined) {
            dl = Text::RenderOutlined(dl, &x, &y, line.text, font, line.colour, line.outline, screen.x, screen.y, 0, 0);
        } else {
            dl = Text::Render(dl, &x, &y, line.text, font, line.colour, screen.x, screen.y, 0, 0);
        }
    }
    return Text::RestoreRdp(dl);
}

} // namespace Frigate::Level
