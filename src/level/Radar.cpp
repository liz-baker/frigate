#include "level/Radar.h"

#include <cmath>

#include "game/TextRender.h"
#include "gfx/GbiF3dex.h"
#include "level/ImageDraw.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kPi = 3.1415927f;

// The circle's entry in the global image table (s_mpradarimages).
constexpr int32_t kRadarImage = 0x1020;

// The circle is a 32 by 32 square, 26 below the view's top and 41 in from its
// right edge; the dots' orbit is 16 pixels across for 4000 world units.
constexpr int32_t kCircleHalf = 0x10;
constexpr int32_t kTopOffset = 0x1A;
constexpr int32_t kRightInset = 0x29;
constexpr int32_t kLeftColumnShift = 0xF;
constexpr float kOrbitRadius = 16.0f;
constexpr float kOrbitRange = 4000.0f;

constexpr uint32_t kShadow = 0x00000040;
constexpr uint32_t kSelfSolo = 0xFFFFFFA0;
constexpr uint32_t kSelfWithoutItem = 0xFF7777FF;
constexpr uint32_t kSelfWithItem = 0x8888FFFF;
constexpr uint32_t kTeamInside = 0xFF0000A0;
constexpr uint32_t kTeamOutside = 0xFF000060;
constexpr uint32_t kItemInside = 0x2828FFFF;
constexpr uint32_t kItemOutside = 0x2828FFB0;
constexpr uint32_t kOtherInside = 0xFFFF00A0;
constexpr uint32_t kOtherOutside = 0xFFFF0060;

} // namespace

int32_t RadarLeft(const ViewMetrics& vi, int32_t playerCount, int32_t player) {
    int32_t left = (vi.viewLeft + vi.viewWidth) - kRightInset;
    if (playerCount >= 3 && (player & 1) == 0) {
        left += kLeftColumnShift;
    }
    return left;
}

int32_t RadarTop(const ViewMetrics& vi) {
    return vi.viewTop + kTopOffset;
}

RadarLayout LayoutRadar(const ViewMetrics& vi, const RadarFrame& frame) {
    RadarLayout layout;
    const RadarPlayer& self = frame.players[static_cast<size_t>(frame.player)];
    if (frame.playerCount == 1 || frame.menuOn || self.dead || frame.disabled) {
        return layout;
    }
    layout.shown = true;
    layout.left = RadarLeft(vi, frame.playerCount, frame.player);
    layout.top = RadarTop(vi);

    if (frame.teamColours) {
        layout.selfColour = self.haveTokenOrGoldenGun == 0 ? kSelfWithoutItem : kSelfWithItem;
    } else {
        layout.selfColour = kSelfSolo;
    }

    for (int32_t i = 0; i < frame.playerCount; i++) {
        const RadarPlayer& other = frame.players[static_cast<size_t>(i)];
        if (i == frame.player || other.dead) {
            continue;
        }
        const float dx = other.x - self.x;
        const float dz = other.z - self.z;
        const float angle = ((GameAtan2f(dx, dz) * 180.0f) / kPi) + frame.theta + 180.0f;
        float distance = std::sqrt((dx * dx) + (dz * dz)) * (kOrbitRadius / kOrbitRange);

        uint32_t colour;
        const bool inside = distance < kOrbitRadius;
        if (!inside) {
            distance = kOrbitRadius;
        }
        if (frame.teamColours) {
            if (other.haveTokenOrGoldenGun == 0) {
                colour = inside ? kTeamInside : kTeamOutside;
            } else {
                colour = inside ? kItemInside : kItemOutside;
            }
        } else {
            colour = inside ? kOtherInside : kOtherOutside;
        }

        RadarBlip& blip = layout.blips[static_cast<size_t>(layout.blipCount++)];
        blip.x = static_cast<int32_t>(GameSinf(angle * 0.017453292f) * distance) + layout.left;
        blip.y = static_cast<int32_t>(GameCosf(angle * 0.017453292f) * distance) + layout.top;
        blip.colour = colour;
    }
    return layout;
}

Gfx* RenderRadar(Gfx* dl, const ViewMetrics& vi, const RadarFrame& frame) {
    using namespace Frigate::F3dex;
    namespace Text = Frigate::Game::Text;

    const RadarLayout layout = LayoutRadar(vi, frame);
    if (!layout.shown) {
        return dl;
    }

    dl = SelectEffectImage(dl, kRadarImage, 2, 0, false);
    dl = Text::SetupRdp(dl);
    gDPSetCombineLERP(dl++, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, TEXEL0, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, TEXEL0, 0);
    gDPSetPrimColor(dl++, 0, 0, 0x00, 0x00, 0x00, 0xA0);
    gSPTextureRectangle(dl++, (layout.left - kCircleHalf) << 2, (layout.top - kCircleHalf) << 2,
                        (layout.left + kCircleHalf) << 2, (layout.top + kCircleHalf) << 2, G_TX_RENDERTILE, 0x10, 0x10,
                        0x400, 0x400);

    dl = Text::FillRect(dl, layout.left - 2, layout.top - 2, layout.left + 2, layout.top + 2, kShadow);
    dl = Text::FillRect(dl, layout.left - 1, layout.top - 1, layout.left + 1, layout.top + 1, layout.selfColour);
    for (int32_t i = 0; i < layout.blipCount; i++) {
        const RadarBlip& blip = layout.blips[static_cast<size_t>(i)];
        dl = Text::FillRect(dl, blip.x - 2, blip.y - 2, blip.x + 2, blip.y + 2, kShadow);
        dl = Text::FillRect(dl, blip.x - 1, blip.y - 1, blip.x + 1, blip.y + 1, blip.colour);
    }
    return Text::RestoreRdp(dl);
}

} // namespace Frigate::Level
