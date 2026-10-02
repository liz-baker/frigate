#include "level/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/TextRender.h"
#include "game/menu/BloodTexture.h"
#include "gfx/Gu.h"
#include "gfx/GbiF3dex.h"
#include "level/Matrix.h"
#include "level/TextTable.h"
#include "ship/utils/StrHash64.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace Text = Frigate::Game::Text;

namespace {

constexpr float kPi = 3.14159265f;

// The US timings, in ticks (BONDVIEW_INTRO_CAMERA_BONDMESSCNT_A..C and
// BONDVIEW_UPPER_TEXT_TIMER_A..C).
constexpr int32_t kBottomQueuedTimer = 0x1f;
constexpr int32_t kBottomQueuedShow = 0x1e;
constexpr int32_t kBottomLoneShow = 0x78;
constexpr int32_t kTopQueuedTimer = 0x3d;
constexpr int32_t kTopQueuedShow = 0x3c;
constexpr int32_t kTopLoneShow = 0xf0;

// The most characters a strip's message keeps (MAXMESSAGELEN and
// BONDVIEW_HUD_MSG_TOP_BUFFER_LENGTH - 1).
constexpr size_t kBottomMessageLength = 100;
constexpr size_t kTopMessageLength = 0x96;

// BONDVIEW_VIEW_TOP_OFFSET_1 and _2 (US): how far above the view's bottom
// the bottom strip sits, without and with an ammo counter under it.
constexpr int32_t kBottomStripOffsetPlain = 0x0C;
constexpr int32_t kBottomStripOffsetAmmo = 0x28;

// The crosshair's entry in the global image table.
constexpr int32_t kCrosshairImage = 0xD2C;

// HUDHALIGN_* and HUDVALIGN_*.
constexpr int32_t kAlignRightOrBottom = 0;
constexpr int32_t kAlignLeftOrTop = 1;
constexpr int32_t kAlignMiddle = 2;

constexpr uint32_t kTextWhite = 0xFFFFFFFF;
constexpr uint32_t kOutline = 0x646464FF;

std::string Truncate(std::string_view text, size_t length) {
    return std::string(text.substr(0, length));
}

// texSetRenderMode for a single cycle without depth: the render mode a
// texSelect asks for.
Gfx* SetImageRenderMode(Gfx* dl, int32_t mode) {
    using namespace Frigate::F3dex;
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    switch (mode) {
        case 2:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
            break;
        case 3:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_TEX_EDGE | G_RM_AA_TEX_EDGE2);
            break;
        case 4:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_CLD_SURF | G_RM_CLD_SURF2);
            break;
        default:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_OPA_SURF | G_RM_AA_OPA_SURF2);
            break;
    }
    return dl;
}

// texSelect for a plain (level 0) table entry: the render mode, the texture
// on, and the entry's load, which the packer wrote as the list
// `hud/img/<offset>`.
Gfx* SelectImage(Gfx* dl, int32_t offset, int32_t mode) {
    using namespace Frigate::F3dex;
    dl = SetImageRenderMode(dl, mode);
    *dl++ = Texture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, 1);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    gDPSetCombineMode(dl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
    char path[32];
    std::snprintf(path, sizeof(path), "hud/img/%04x", static_cast<unsigned>(offset));
    return DisplayListByHash(dl, CRC64(path));
}

// draw_textured_rectangle: the rectangle centred on `position`, `size` being
// its half extents.
Gfx* DrawTexturedRectangle(Gfx* dl, const float position[2], const float size[2], int32_t width, int32_t height,
                           bool flipXY, bool flipX, bool flipY) {
    using namespace Frigate::F3dex;
    if (!(size[0] > 0.0f && size[1] > 0.0f)) {
        return dl;
    }
    int32_t s = 0;
    int32_t t = 0;

    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);

    int32_t xl = static_cast<int32_t>((position[0] - size[0]) * 4.0f);
    int32_t yl = static_cast<int32_t>((position[1] - size[1]) * 4.0f);
    const int32_t xh = static_cast<int32_t>((position[0] + size[0]) * 4.0f);
    const int32_t yh = static_cast<int32_t>((position[1] + size[1]) * 4.0f);

    if (xh >= 0 && yh >= 0) {
        if (xl < 0) {
            if (flipXY) {
                t += ((-xl * height) << 5) / (xh - xl);
            } else {
                s += ((-xl * width) << 5) / (xh - xl);
            }
            xl = 0;
        }
        if (yl < 0) {
            if (flipXY) {
                s += ((-yl * width) << 5) / (yh - yl);
            } else {
                t += ((-yl * height) << 5) / (yh - yl);
            }
            yl = 0;
        }

        int32_t dsdx;
        int32_t dtdy;
        if (flipXY) {
            dsdx = static_cast<int32_t>(static_cast<float>(width) / (2.0f * size[1]) * 1024.0f);
            dtdy = static_cast<int32_t>(static_cast<float>(height) / (2.0f * size[0]) * 1024.0f);
        } else {
            dsdx = static_cast<int32_t>(static_cast<float>(width) / (2.0f * size[0]) * 1024.0f);
            dtdy = static_cast<int32_t>(static_cast<float>(height) / (2.0f * size[1]) * 1024.0f);
        }
        if (flipX) {
            dsdx = 0x10000 - dsdx;
            s = ((width - 1) << 5) - s;
        }
        if (flipY) {
            dtdy = 0x10000 - dtdy;
            t = ((height - 1) << 5) - t;
        }

        if (flipXY) {
            gSPTextureRectangleFlip(dl++, xl, yl, xh, yh, G_TX_RENDERTILE, s, t, dsdx, dtdy);
        } else {
            gSPTextureRectangle(dl++, xl, yl, xh, yh, G_TX_RENDERTILE, s, t, dsdx, dtdy);
        }
    }

    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    return dl;
}

// display_image_at_position for a plain image with no highlight: the
// environment colour tints it, and its alpha is the environment's.
Gfx* DisplayImageAtPosition(Gfx* dl, const float position[2], const float size[2], int32_t width, int32_t height,
                            bool flipXY, bool flipX, bool flipY, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    if (!(size[0] > 0.0f && size[1] > 0.0f)) {
        return dl;
    }
    gDPSetEnvColor(dl++, r, g, b, alpha);
    gDPSetCombineLERP(dl++, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0,
                      ENVIRONMENT, 0);
    return DrawTexturedRectangle(dl, position, size, width, height, flipXY, flipX, flipY);
}

// The plain-integer text the ammo counter draws: the format is "%d\n", whose
// newline gives the text its height.
std::string HudInteger(int32_t value) {
    return std::to_string(value) + "\n";
}

} // namespace

void MakeDamageSegments(GaugeVertices* vertices, int32_t isArmour, float value) {
    value *= 8.0f;
    Vtx_t* out = vertices->data();

    for (int32_t i = 0; i < 23; i++) {
        // The angle starts at 142 degrees (the half degree truncated) and
        // steps down five per segment.
        const auto degrees = static_cast<float>(static_cast<int32_t>(142.5 - static_cast<double>(i * 5)));
        const float angle = (degrees * kPi * 2.0f) / 360.0f;

        for (int32_t pair = 0; pair < 2; pair++) {
            const auto offsetX =
                static_cast<int16_t>((((GameSinf(angle) * 4.0f * 130.0f * static_cast<float>(6 - pair)) / 5.0f) *
                                      static_cast<float>(isArmour)));
            const auto offsetZ =
                static_cast<int16_t>((((GameCosf(angle) * 4.0f) * 130.0f * static_cast<float>(6 - pair)) / 5.0f));

            Vtx_t& vertex = *out++;
            vertex.ob[0] = static_cast<int16_t>(offsetX + 1);
            vertex.ob[1] = 0;
            vertex.ob[2] = static_cast<int16_t>(-offsetZ);
            vertex.flag = 0;
            vertex.tc[0] = 0;
            vertex.tc[1] = 0;
            vertex.cn[0] = 255;
            vertex.cn[1] = 255;
            vertex.cn[2] = 255;

            if (isArmour >= 1) {
                vertex.cn[0] = static_cast<uint8_t>(static_cast<int32_t>(96.0f - (GameCosf(angle) * 96.0f)));
                vertex.cn[1] = static_cast<uint8_t>(static_cast<int32_t>(127.0f - (GameCosf(angle) * 127.0f)));
                vertex.cn[2] = 255;
            } else if (isArmour < 0) {
                vertex.cn[1] = static_cast<uint8_t>(static_cast<int32_t>(127.0f - (GameCosf(angle) * 127.0f)));
                vertex.cn[2] = static_cast<uint8_t>(static_cast<int32_t>(32.0f - (GameCosf(angle) * 32.0f)));
            }

            // The first ten segments are single and the rest come in pairs
            // with a gap: each is full, part filled or empty.
            const auto whole = static_cast<int32_t>(value);
            const auto partial =
                static_cast<uint8_t>(static_cast<int32_t>((value - static_cast<float>(whole)) * 207.0f) + 48);
            if (i < 10) {
                if (i <= (whole * 2) - 1) {
                    vertex.cn[3] = 255;
                } else if (i < static_cast<int32_t>(value * 2.0f) && i > (whole * 2) - 1) {
                    vertex.cn[3] = partial;
                } else {
                    vertex.cn[3] = 48;
                }
            } else {
                if (static_cast<float>(i) <= (9.0f + ((value - 5.0f) * 4.0f))) {
                    vertex.cn[3] = 255;
                } else if (i <= (static_cast<int32_t>(((value - 5.0f) * 4.0f) + 0.5f) + 9) &&
                           i > ((static_cast<int32_t>(value - 5.0f) * 2) + 8)) {
                    vertex.cn[3] = partial;
                } else {
                    vertex.cn[3] = 48;
                }
            }
        }
    }
}

void Hud::Reset() {
    sightMode_ = 0;
    ammoOff_ = 0;
    messagesOff_ = 0;
    upperTextFlag_ = 0;
    messageFont_ = HudFont::Gothic;
    bottomTimer_ = -1;
    bottomCount_ = 0;
    bottomIndex_ = 0;
    topTimer_ = -1;
    topCount_ = 0;
    topIndex_ = 0;
    health_ = HealthDisplay{};
}

bool Hud::SetImages(std::string_view text) {
    images_.clear();
    for (std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        const auto fields = Split(line, '\t');
        HudImage image;
        if (fields.size() < 3 || !ParseNumber(fields[0], image.offset, 16) || !ParseNumber(fields[1], image.width) ||
            !ParseNumber(fields[2], image.height)) {
            images_.clear();
            return false;
        }
        images_.push_back(image);
    }
    return true;
}

const HudImage* Hud::Image(int32_t offset) const {
    for (const HudImage& image : images_) {
        if (image.offset == offset) {
            return &image;
        }
    }
    return nullptr;
}

void Hud::SetSightVisible(int32_t reason, bool visible) {
    if (visible) {
        sightMode_ &= ~reason;
    } else {
        sightMode_ |= reason;
    }
}

void Hud::SetAmmoVisible(int32_t reason, bool visible) {
    if (visible) {
        ammoOff_ &= ~reason;
    } else {
        ammoOff_ |= reason;
    }
}

void Hud::BottomShow(std::string_view text) {
    if (bottomCount_ < kBottomMessageSlots) {
        const int32_t slot = (bottomIndex_ + bottomCount_) % kBottomMessageSlots;
        bottomText_[static_cast<size_t>(slot)] = Truncate(text, kBottomMessageLength);
        bottomCount_++;
    }
}

void Hud::TopShow(std::string_view text) {
    if (topCount_ >= kTopMessageSlots) {
        return;
    }
    const int32_t slot = (topIndex_ + topCount_) % kTopMessageSlots;
    topText_[static_cast<size_t>(slot)] = Truncate(text, kTopMessageLength);
    topCount_++;
}

std::string_view Hud::BottomMessage() const {
    return bottomText_[static_cast<size_t>(bottomIndex_)];
}

std::string_view Hud::TopMessage() const {
    return topText_[static_cast<size_t>(topIndex_)];
}

const Font* Hud::MessageFontBank() const {
    return messageFont_ == HudFont::Zurich ? zurich_.get() : gothic_.get();
}

void Hud::TickBottomMessage(int32_t clockTimer) {
    if (messagesOff_ != 0) {
        return;
    }
    if (bottomTimer_ >= 0) {
        bottomTimer_ -= clockTimer;
        if (bottomTimer_ < 0) {
            bottomIndex_ = (bottomIndex_ + 1) % kBottomMessageSlots;
            bottomCount_ -= 1;
        } else if (bottomCount_ >= 2 && bottomTimer_ >= kBottomQueuedTimer) {
            bottomTimer_ = kBottomQueuedShow;
        }
    }
    if (bottomTimer_ < 0 && bottomCount_ > 0) {
        bottomTimer_ = bottomCount_ >= 2 ? kBottomQueuedShow : kBottomLoneShow;
    }
}

void Hud::TickTopMessage(int32_t clockTimer) {
    if (upperTextFlag_ != 0) {
        return;
    }
    if (topTimer_ >= 0) {
        topTimer_ -= clockTimer;
        if (topTimer_ < 0) {
            topIndex_ = (topIndex_ + 1) % kTopMessageSlots;
            topCount_ -= 1;
        } else if (topCount_ >= 2 && topTimer_ >= kTopQueuedTimer) {
            topTimer_ = kTopQueuedShow;
        }
    }
    if (topTimer_ < 0 && topCount_ > 0) {
        topTimer_ = topCount_ >= 2 ? kTopQueuedShow : kTopLoneShow;
    }
}

Gfx* Hud::RenderBottomMessage(Gfx* dl, const HudFrame& frame) {
    if (messagesOff_ != 0 || bottomTimer_ < 0) {
        return dl;
    }
    const std::string& text = bottomText_[static_cast<size_t>(bottomIndex_)];
    const Font* font = MessageFontBank();
    if (text.empty() || font == nullptr) {
        return dl;
    }

    dl = Text::SetupRdp(dl);
    const Text::Extent extent = Text::Measure(*font, text);

    int32_t left = frame.vi.viewLeft + 0x1E;
    int32_t right = left + extent.width;
    // The strip sits above the ammo counter's icon when the left hand holds a
    // weapon with ammo, or a mission clock is showing (is_clock_drawn_onscreen).
    const bool clockDrawn = frame.countdownShown;
    const int32_t leftItem = frame.weapons != nullptr ? frame.weapons->WeaponId(HandLeft) : 0;
    const bool leftAmmo = frame.weapons != nullptr && frame.weapons->Table().AmmoType(leftItem) != 0;
    int32_t bottom = frame.vi.viewTop + frame.vi.viewHeight;
    bottom -= (!leftAmmo && !clockDrawn) ? kBottomStripOffsetPlain : kBottomStripOffsetAmmo;
    int32_t top = bottom - extent.height;

    dl = Text::DrawBlackBox(dl, left, top, right, bottom);
    int32_t x = left;
    int32_t y = top;
    dl = Text::RenderOutlined(dl, &x, &y, text, *font, kTextWhite, kOutline, frame.vi.x, frame.vi.y, 0, 0);
    return Text::RestoreRdp(dl);
}

Gfx* Hud::RenderTopMessage(Gfx* dl, const HudFrame& frame) {
    if (upperTextFlag_ != 0 || topTimer_ < 0) {
        return dl;
    }
    const std::string& text = topText_[static_cast<size_t>(topIndex_)];
    if (text.empty() || zurich_ == nullptr) {
        return dl;
    }

    dl = Text::SetupRdp(dl);
    const Text::Extent extent = Text::Measure(*zurich_, text);
    // TODO(https://github.com/liz-baker/frigate/issues/222): the strip's position
    // in a security camera view.
    int32_t x = frame.vi.viewLeft + 0x1E;
    int32_t y = frame.vi.viewTop + 0xD;
    const int32_t bottom = y + extent.height;
    dl = Text::FillRect(dl, 0, y - 2, frame.vi.x, bottom, 0x64);
    dl = Text::Render(dl, &x, &y, text, *zurich_, kTextWhite, frame.vi.x, frame.vi.y, 0, 0);
    return Text::RestoreRdp(dl);
}

Gfx* Hud::RenderGaugeBars(Gfx* dl, const HudFrame& frame) {
    using namespace Frigate::F3dex;

    MakeDamageSegments(&armourVertices_, 1, health_.armour);
    MakeDamageSegments(&healthVertices_, -1, health_.health);

    // An orthographic view of the gauges from above.
    Gu::MatrixF ortho;
    const float visibility = frame.visibilityScale;
    Gu::OrthoF(ortho, -800.0f * visibility, 800.0f * visibility, -600.0f * visibility, 600.0f * visibility, -100.0f,
               1000.0f, 1.0f);
    gaugeProjection_ = Mtx::ToFixedMtx(ortho);
    *dl++ = Matrix(&gaugeProjection_, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    Gu::MatrixF lookAt;
    Gu::LookAtF(lookAt, 0.0f, 500.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    gaugeView_ = Mtx::ToFixedMtx(lookAt);
    *dl++ = Matrix(&gaugeView_, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);

    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetPrimColor(dl++, 0, 0, 0xE6, 0xE6, 0xE6, 0x00);
    *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);

    dl = EmitWatchGauges(dl);

    if (frame.projection != nullptr) {
        *dl++ = Matrix(frame.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    }
    return dl;
}

void Hud::MakeWatchGauges(float armour, float health) {
    MakeDamageSegments(&armourVertices_, 1, armour);
    MakeDamageSegments(&healthVertices_, -1, health);
}

Gfx* Hud::EmitWatchGauges(Gfx* dl) const {
    using namespace Frigate::F3dex;
    // buildGaugeBarDL: each step draws the quad between two pairs of
    // vertices; the first ten steps alternate, and after that every fourth
    // is skipped for the gap.
    for (const GaugeVertices* bar : { &armourVertices_, &healthVertices_ }) {
        for (int32_t i = 0; i <= (static_cast<int32_t>(kGaugeVertexCount) / 2) - 2; i++) {
            const Vtx_t* first = bar->data() + (i * 2);
            *dl++ = Vertices(first, 4, 0);
            const bool draw = i >= 9 ? ((i + 3) % 4) != 0 : (i & 1) == 0;
            if (draw) {
                *dl++ = Triangle(0, 1, 2);
                *dl++ = Triangle(1, 2, 3);
            }
        }
    }
    return dl;
}

Gfx* Hud::RenderSight(Gfx* dl, const HudFrame& frame) {
    if (sightMode_ != 0) {
        return dl;
    }
    const HudImage* image = Image(kCrosshairImage);
    if (image == nullptr || frame.weapons == nullptr) {
        return dl;
    }
    dl = SelectImage(dl, kCrosshairImage, 4);

    const float position[2] = { frame.weapons->Aim().crosshairScreenX, frame.weapons->Aim().crosshairScreenY };
    float size[2] = { 16.0f, 16.0f };
    if (frame.ratio16x9) {
        size[0] = size[0] * 0.75f;
    }
    return DisplayImageAtPosition(dl, position, size, 0x20, 0x20, false, false, true, 0xFF, 0xFF, 0xFF, 0x6E);
}

Gfx* Hud::DrawAmmoIcon(Gfx* dl, int32_t imageOffset, float x, float y, float belowIcon, bool flipped, float yOffset) {
    const HudImage* image = Image(imageOffset);
    if (image == nullptr) {
        return dl;
    }
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_DISABLE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_POINT);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);

    // An odd size puts the icon half a pixel off, which mirrors with the hand.
    float position[2];
    position[0] = (static_cast<float>(image->width) * 0.5f) - static_cast<float>(image->width / 2);
    if (flipped) {
        position[0] = -position[0];
    }
    position[0] = position[0] + x;
    if (0.0f <= y) {
        position[1] = y - (static_cast<float>(image->height) * 0.5f);
    } else {
        position[1] = -((static_cast<float>(image->height) * 0.5f) - static_cast<float>(image->height / 2));
        position[1] = belowIcon + position[1];
        position[1] = position[1] + yOffset;
    }

    const float half[2] = { static_cast<float>(image->width) * 0.5f, static_cast<float>(image->height) * 0.5f };

    // A transparent box behind the icon, then the icon itself.
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, 0, 0, 0, 0);
    gDPFillRectangle(dl++, static_cast<int32_t>(position[0] - half[0]) - 1,
                     static_cast<int32_t>(position[1] - half[1]) - 1, static_cast<int32_t>(position[0] + half[0]) + 1,
                     static_cast<int32_t>(position[1] + half[1]) + 1);

    dl = SelectImage(dl, imageOffset, 2);
    dl = DisplayImageAtPosition(dl, position, half, image->width, image->height, false, false, true, 0xFF, 0xFF, 0xFF,
                                0xFF);

    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_LOD);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    return dl;
}

Gfx* Hud::DrawString(Gfx* dl, const HudFrame& frame, std::string_view text, int32_t x, int32_t halign, int32_t y,
                     int32_t valign) {
    // gunDrawHudString always draws with the outline the ammo counter uses.
    if (gothic_ == nullptr) {
        return dl;
    }
    const Text::Extent extent = Text::Measure(*gothic_, text);
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;

    if (halign == kAlignLeftOrTop) {
        x2 = x + extent.width;
        x1 = x;
    } else if (halign == kAlignRightOrBottom) {
        x1 = x - extent.width;
        x2 = x;
    } else if (halign == kAlignMiddle) {
        x2 = x + (extent.width / 2);
        x1 = x2 - extent.width;
    }

    if (valign == kAlignLeftOrTop) {
        y2 = y + extent.height;
        y1 = y;
    } else if (valign == kAlignRightOrBottom) {
        y1 = y - extent.height;
        y2 = y;
    } else if (valign == kAlignMiddle) {
        y2 = y + (extent.height / 2);
        y1 = y2 - extent.height;
    }

    dl = Text::DrawBlackBox(dl, x1, y1, x2, y2);
    return Text::RenderOutlined(dl, &x1, &y1, text, *gothic_, kTextWhite, kOutline, frame.vi.x, frame.vi.y, 0, 0);
}

Gfx* Hud::RenderCountdown(Gfx* dl, const HudFrame& frame) {
    if (!frame.countdownShown) {
        return dl;
    }
    float time = frame.countdownTime;
    if (time < 0.0f) {
        time = -time;
    }

    const auto mins = static_cast<int32_t>(std::floor(time / 3600.0f));
    const auto secs = static_cast<int32_t>(std::floor(time / 60.0f)) - (mins * 60);
    const auto hundredths = (static_cast<int32_t>(std::floor((time * 100.0f) / 60.0f)) - (mins * 6000)) - (secs * 100);

    dl = Text::SetupRdp(dl);
    // The text sits 18 pixels above the bottom of the view.
    const int32_t y = (frame.vi.viewTop + frame.vi.viewHeight) - 18;
    const std::string_view colon = ":\n";
    dl = DrawString(dl, frame, HudInteger((mins % 100) / 10), 0x82, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, HudInteger(mins % 10), 0x8A, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, colon, 0x93, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, HudInteger((secs % 60) / 10), 0x9C, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, HudInteger(secs % 10), 0xA4, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, colon, 0xAD, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, HudInteger((hundredths % 100) / 10), 0xB6, kAlignMiddle, y, kAlignMiddle);
    dl = DrawString(dl, frame, HudInteger(hundredths % 10), 0xBE, kAlignMiddle, y, kAlignMiddle);
    return Text::RestoreRdp(dl);
}

Gfx* Hud::RenderAmmo(Gfx* dl, const HudFrame& frame) {
    if (ammoOff_ != 0 || frame.weapons == nullptr) {
        return dl;
    }
    const PlayerWeapons& weapons = *frame.weapons;
    const WeaponTable& table = weapons.Table();
    const ViewMetrics& vi = frame.vi;

    const int32_t weaponLeft = weapons.WeaponId(HandLeft);
    const int32_t weaponRight = weapons.WeaponId(HandRight);
    // One player: both counters sit 59 pixels in from their side.
    const int32_t leftX = 59;
    const int32_t rightX = 59;
    const auto viewBottom = static_cast<float>(vi.viewTop + vi.viewHeight);
    const int32_t counterY = (vi.viewTop + vi.viewHeight) - 18;

    const auto swapping = [&weapons](Hand hand) {
        const int32_t state = weapons.Hands(hand).actionState;
        return state == GunState::SwitchSwap || state == GunState::SwitchHold;
    };

    if (weaponRight != kItemUnarmed) {
        const int32_t ammoType = table.AmmoType(weaponRight);
        if (ammoType != 0 && !swapping(HandRight) && !table.HasFlag(weaponRight, WeaponFlag::HideAmmoDisplay)) {
            const AmmoIcon icon = table.Icon(ammoType);
            int32_t textWidth = 5;
            if (icon.image != 0) {
                const float screenLeft = static_cast<float>(vi.viewLeft);
                const float screenWidth = static_cast<float>(vi.viewWidth);
                dl = DrawAmmoIcon(dl, icon.image, (screenLeft + screenWidth) - static_cast<float>(rightX), -1.0f,
                                  viewBottom - 20.0f, false, icon.yOffset);
                if (const HudImage* image = Image(icon.image)) {
                    textWidth = image->width;
                }
            }

            dl = Text::SetupRdp(dl);

            int32_t magazine;
            int32_t reserve;
            if (table.HasFlag(weaponRight, WeaponFlag::NoClipReloads)) {
                magazine = 0;
                reserve = weapons.AmmoHeld(ammoType) + weapons.Hands(HandRight).ammoInMagazine;
                if (weaponLeft == weaponRight) {
                    reserve += weapons.Hands(HandLeft).ammoInMagazine;
                }
            } else {
                magazine = weapons.Hands(HandRight).ammoInMagazine;
                reserve = weapons.AmmoHeld(ammoType);
            }

            if (!table.HasFlag(weaponRight, WeaponFlag::NoClipReloads)) {
                dl = DrawString(dl, frame, HudInteger(magazine),
                                (((vi.viewLeft + vi.viewWidth) - rightX) - (textWidth / 2)) - 4, kAlignRightOrBottom,
                                counterY, kAlignMiddle);
            }
            if (reserve > 0 || table.HasFlag(weaponRight, WeaponFlag::NoClipReloads)) {
                dl = DrawString(dl, frame, HudInteger(reserve),
                                (((vi.viewLeft + vi.viewWidth) - rightX) + ((textWidth + 1) / 2)) + 3, kAlignLeftOrTop,
                                counterY, kAlignMiddle);
            }
            dl = Text::RestoreRdp(dl);
        }
    }

    if (weaponLeft != kItemUnarmed) {
        const int32_t ammoType = table.AmmoType(weaponLeft);
        if (ammoType != 0 && !swapping(HandLeft) && !table.HasFlag(weaponLeft, WeaponFlag::HideAmmoDisplay)) {
            const AmmoIcon icon = table.Icon(ammoType);
            int32_t textWidth = 5;
            if (icon.image != 0) {
                dl = DrawAmmoIcon(dl, icon.image, static_cast<float>(vi.viewLeft) + static_cast<float>(leftX), -1.0f,
                                  viewBottom - 20.0f, true, icon.yOffset);
                if (const HudImage* image = Image(icon.image)) {
                    textWidth = image->width;
                }
            }

            dl = Text::SetupRdp(dl);

            int32_t magazine;
            int32_t reserve;
            if (table.HasFlag(weaponLeft, WeaponFlag::NoClipReloads)) {
                magazine = 0;
                reserve = weapons.AmmoHeld(ammoType) + weapons.Hands(HandLeft).ammoInMagazine;
                if (weaponLeft == weaponRight) {
                    reserve += weapons.Hands(HandRight).ammoInMagazine;
                }
            } else {
                magazine = weapons.Hands(HandLeft).ammoInMagazine;
                reserve = weapons.AmmoHeld(ammoType);
            }

            if (!table.HasFlag(weaponLeft, WeaponFlag::NoClipReloads)) {
                dl = DrawString(dl, frame, HudInteger(magazine), ((vi.viewLeft + leftX) + (textWidth / 2)) + 3,
                                kAlignLeftOrTop, counterY, kAlignMiddle);
            }
            if (reserve > 0 || table.HasFlag(weaponLeft, WeaponFlag::NoClipReloads)) {
                dl = DrawString(dl, frame, HudInteger(reserve), ((vi.viewLeft + leftX) - ((textWidth + 1) / 2)) - 4,
                                kAlignRightOrBottom, counterY, kAlignMiddle);
            }
            dl = Text::RestoreRdp(dl);
        }
    }
    return dl;
}

Gfx* Hud::RenderWatchAmmo(Gfx* dl, const HudFrame& frame) {
    if (frame.weapons == nullptr) {
        return dl;
    }
    const PlayerWeapons& weapons = *frame.weapons;
    const WeaponTable& table = weapons.Table();
    const int32_t weaponLeft = weapons.WeaponId(HandLeft);
    const int32_t item = weapons.WeaponId(HandRight);
    if (item == kItemUnarmed) {
        return dl;
    }
    const int32_t ammoType = table.AmmoType(item);
    const int32_t state = weapons.Hands(HandRight).actionState;
    if (ammoType == 0 || state == GunState::SwitchSwap || state == GunState::SwitchHold ||
        table.HasFlag(item, WeaponFlag::HideAmmoDisplay)) {
        return dl;
    }

    const AmmoIcon icon = table.Icon(ammoType);
    int32_t textWidth = 5;
    if (icon.image != 0) {
        dl = DrawAmmoIcon(dl, icon.image, 200.0f, 180.0f,
                          static_cast<float>((frame.vi.viewTop + frame.vi.viewHeight) - 20), false, icon.yOffset);
        if (const HudImage* image = Image(icon.image)) {
            textWidth = image->width;
        }
    }

    dl = Text::SetupRdp(dl);

    int32_t magazine;
    int32_t reserve;
    if (table.HasFlag(item, WeaponFlag::NoClipReloads)) {
        magazine = 0;
        reserve = weapons.AmmoHeld(ammoType) + weapons.Hands(HandRight).ammoInMagazine;
        if (weaponLeft == item) {
            reserve += weapons.Hands(HandLeft).ammoInMagazine;
        }
    } else {
        magazine = weapons.Hands(HandRight).ammoInMagazine;
        reserve = weapons.AmmoHeld(ammoType);
    }

    if (!table.HasFlag(item, WeaponFlag::NoClipReloads)) {
        dl = DrawString(dl, frame, HudInteger(magazine), 196 - (textWidth / 2), kAlignRightOrBottom, 177, kAlignMiddle);
    }
    if (reserve > 0 || table.HasFlag(item, WeaponFlag::NoClipReloads)) {
        dl =
            DrawString(dl, frame, HudInteger(reserve), 203 + ((textWidth + 1) / 2), kAlignLeftOrTop, 177, kAlignMiddle);
    }
    return Text::RestoreRdp(dl);
}

Gfx* Hud::RenderIntroCamera(Gfx* dl, const HudFrame& frame) {
    TickBottomMessage(frame.clockTimer);
    dl = RenderBottomMessage(dl, frame);
    TickTopMessage(frame.clockTimer);
    dl = RenderTopMessage(dl, frame);
    dl = RenderCountdown(dl, frame);
    // TODO(https://github.com/liz-baker/frigate/issues/140): bondviewRenderCredits
    // (Cuba's).
    return RenderColourScreen(dl, frame);
}

Gfx* Hud::RenderColourScreen(Gfx* dl, const HudFrame& frame) {
    // TODO(https://github.com/liz-baker/frigate/issues/136): cameraFrameCounter1 and
    // cameraFrameCounter2, which Cuba's camera buffer and the end-of-level AI command
    // use to black the screen out.
    if (!(frame.colourFrac > 0.0f)) {
        return dl;
    }
    using namespace Frigate::F3dex;
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_DISABLE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_CLD_SURF | G_RM_CLD_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, frame.colourRed, frame.colourGreen, frame.colourBlue,
                    static_cast<int32_t>(frame.colourFrac * 255.0f));
    gDPFillRectangle(dl++, frame.vi.viewLeft, frame.vi.viewTop, frame.vi.viewLeft + frame.vi.viewWidth,
                     frame.vi.viewTop + frame.vi.viewHeight);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_LOD);
    return dl;
}

Gfx* Hud::RenderBloodOverlay(Gfx* dl, const HudFrame& frame) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH);
    *dl++ = Texture(0x8000, 0x8000, 0, G_TX_RENDERTILE, 1);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_CLD_SURF | G_RM_CLD_SURF2);
    gDPSetCombineMode(dl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_MAGICSQ);
    gDPSetPrimColor(dl++, 0, 0, 0x96, 0x00, 0x00, 0xB4);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    dl = Game::Menu::LoadBloodTexture(dl, *frame.blood);
    gSPTextureRectangle(dl++, frame.vi.viewLeft * 4, frame.vi.viewTop * 4,
                        ((frame.vi.viewLeft + frame.vi.viewWidth) * 4) - 1,
                        ((frame.vi.viewTop + frame.vi.viewHeight) * 4) - 1, G_TX_RENDERTILE, 0, 0,
                        0x18000 / frame.vi.viewWidth, 0x14000 / frame.vi.viewHeight);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    return dl;
}

Gfx* Hud::Render(Gfx* dl, const HudFrame& frame) {
    if (health_.showTime > 0 && frame.watchState == 0) {
        dl = RenderGaugeBars(dl, frame);
    } else if (health_.displayTime > 0) {
        // mpwatchShouldDisplayGauges: only the multiplayer watch sets the
        // display time.
        dl = RenderGaugeBars(dl, frame);
        health_.displayTime -= frame.clockTimer;
        if (health_.displayTime < 0) {
            health_.displayTime = 0;
        }
    }

    // display_objective_status_text_on_status_change and the dying player's
    // state machine ran in Stage::Render ahead of this; the blood overlay is
    // what that leaves to draw.
    if (frame.blood != nullptr) {
        dl = RenderBloodOverlay(dl, frame);
    }

    TickBottomMessage(frame.clockTimer);
    dl = RenderBottomMessage(dl, frame);
    TickTopMessage(frame.clockTimer);
    dl = RenderTopMessage(dl, frame);
    dl = RenderSight(dl, frame);
    dl = RenderAmmo(dl, frame);
    dl = RenderCountdown(dl, frame);
    // TODO(https://github.com/liz-baker/frigate/issues/222): display_red_blue_on_radar,
    // the multiplayer radar, and the multiplayer branches of the strips, sight
    // and ammo counter.
    return RenderColourScreen(dl, frame);
}

} // namespace Frigate::Level
