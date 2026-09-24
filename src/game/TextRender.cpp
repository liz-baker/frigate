#include "game/TextRender.h"

#include <algorithm>

#include "game/ScreenSize.h"
#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Text {

namespace {

constexpr int32_t kSpaceWidth = 5;
constexpr int32_t kFirstGlyph = 0x21; // '!'; the bank holds kNumChars glyphs from here

constexpr int32_t kScreenWidth = kMenuScreenWidth;
constexpr int32_t kScreenHeight = kMenuScreenHeight;

const FontChar* Glyph(const Font& font, char c) {
    auto index = static_cast<int32_t>(static_cast<uint8_t>(c)) - kFirstGlyph;
    if (index < 0 || static_cast<size_t>(index) >= font.Chars.size()) {
        return nullptr;
    }
    return &font.Chars[index];
}

int32_t DefaultLineHeight(const Font& font) {
    const FontChar* bracket = Glyph(font, '[');
    return bracket->height + bracket->baseline;
}

int32_t Kerning(const Font& font, const FontChar& previous, const FontChar& current) {
    return font.Kerning[previous.kerningIndex * 13 + current.kerningIndex];
}

// textRenderGlyph() with text_orientation == 0.
Gfx* RenderGlyph(Gfx* dl, int32_t* x, int32_t* y, const FontChar& current, const FontChar& previous, const Font& font,
                 int32_t clipX, int32_t clipY, int32_t clipWidth, int32_t clipHeight, int32_t yOffset) {
    using namespace Frigate::F3dex;

    int32_t drawY = *y + yOffset;
    *x -= Kerning(font, previous, current) - 1;

    int32_t top = current.baseline + drawY;
    int32_t bottom = top + current.height;

    bool onScreen = *x > 0 && *x <= kScreenWidth && top <= kScreenHeight;
    if (onScreen && clipX + clipWidth >= *x && clipY + clipHeight >= top && *x >= clipX && bottom >= clipY) {
        // The 8-bit intensity path; glyph indices of 0x80 and above are the
        // paletted Japanese path (see the TODO in TextRender.h).
        *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
        // gDPLoadTextureBlock() with the image's real row stride in the
        // SETTIMG width: the interpreter takes its row stride from it, and the
        // macro's width of 1 makes it read the glyph as one long row.
        const int32_t stride = FontChar::RowStride(current.width);
        const int32_t line = stride / 8;
        gDPSetTextureImage(dl++, G_IM_FMT_I, G_IM_SIZ_8b, stride, const_cast<uint8_t*>(current.Pixels.data()));
        gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0,
                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0);
        gDPLoadSync(dl++);
        gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, stride * current.height - 1, (2048 + line - 1) / line);
        gDPPipeSync(dl++);
        gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, line, 0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK,
                   0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0);
        gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (stride - 1) << G_TEXTURE_IMAGE_FRAC,
                       (current.height - 1) << G_TEXTURE_IMAGE_FRAC);

        if (*x + current.width <= clipX + clipWidth) {
            int32_t ulx = *x;
            int32_t lrx = *x + current.width;
            int32_t uly = top;
            int32_t lry = bottom;
            int32_t t = 0;
            bool draw = true;
            if (clipY > top) {
                // Clipped at the top: start the rect at the clip edge and skip
                // the same rows of the texture.
                draw = bottom >= clipY;
                uly = clipY;
                t = (clipY - top) << 5;
            } else if (bottom > clipY + clipHeight) {
                draw = top <= clipY + clipHeight;
                lry = clipY + clipHeight;
            }
            if (draw) {
                gSPTextureRectangle(dl++, ulx * 4, uly * 4, lrx * 4, lry * 4, G_TX_RENDERTILE, 0, t, 0x400, 0x400);
            }
        }
    }

    *x += current.width;
    return dl;
}

} // namespace

Extent Measure(const Font& font, std::string_view text, int32_t lineHeight) {
    if (lineHeight == 0) {
        lineHeight = DefaultLineHeight(font);
    }

    Extent extent;
    int32_t longest = 0;
    char previous = 'H';
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == ' ') {
            bool endsLine = i + 1 < text.size() && text[i + 1] == '\n';
            if (!endsLine) {
                extent.width += kSpaceWidth;
            }
            previous = 'H';
        } else if (c == '\n') {
            longest = std::max(longest, extent.width);
            extent.width = 0;
            extent.height += lineHeight;
        } else if (static_cast<uint8_t>(c) < 0x80) {
            const FontChar* current = Glyph(font, c);
            const FontChar* before = Glyph(font, previous);
            if (current == nullptr) {
                continue;
            }
            extent.width += current->width - (Kerning(font, *before, *current) - 1);
            previous = c;
        } else {
            // Two-byte glyph: fixed advance. The trailing byte isn't a character.
            bool wide = static_cast<uint8_t>(c) >= 0xc0;
            extent.width += (wide ? 15 : 11) - (font.Kerning[0] - 1);
            i++;
        }
    }
    extent.width = std::max(extent.width, longest);
    return extent;
}

Gfx* SetupRdp(Gfx* dl) {
    using namespace Frigate::F3dex;
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_DISABLE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    return dl;
}

Gfx* FillRect(Gfx* dl, int32_t left, int32_t top, int32_t right, int32_t bottom, uint32_t colour) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, static_cast<uint8_t>(colour >> 24), static_cast<uint8_t>(colour >> 16),
                    static_cast<uint8_t>(colour >> 8), static_cast<uint8_t>(colour));
    gDPFillRectangle(dl++, left, top, right, bottom);
    gDPSetCombineLERP(dl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    return dl;
}

Gfx* Render(Gfx* dl, int32_t* x, int32_t* y, std::string_view text, const Font& font, uint32_t colour, int32_t width,
            int32_t height, int32_t yOffset, int32_t lineHeight) {
    int32_t savedX = *x;
    int32_t savedY = *y;
    char previous = 'H';

    if (lineHeight == 0) {
        lineHeight = DefaultLineHeight(font);
    }

    gDPSetPrimColor(dl++, 0, 0, static_cast<uint8_t>(colour >> 24), static_cast<uint8_t>(colour >> 16),
                    static_cast<uint8_t>(colour >> 8), static_cast<uint8_t>(colour));

    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == ' ') {
            *x += kSpaceWidth;
            previous = 'H';
        } else if (c == '\n') {
            previous = 'H';
            // overlap_correction is -1, so the line always advances by the full height.
            *y += lineHeight;
            *x = savedX;
        } else if (static_cast<uint8_t>(c) < 0x80) {
            const FontChar* current = Glyph(font, c);
            const FontChar* before = Glyph(font, previous);
            if (current == nullptr) {
                continue;
            }
            dl = RenderGlyph(dl, x, y, *current, *before, font, savedX, savedY, width, height, yOffset);
            previous = c;
        } else {
            i++;
        }
    }
    return dl;
}

Gfx* DrawAlignedWhite(Gfx* dl, int32_t x, int32_t y, int32_t halign, int32_t valign, std::string_view text,
                      const Font& font) {
    Extent extent = Measure(font, text);
    x -= (halign * extent.width) / 2;
    y -= (valign * extent.height) / 2;
    return Render(dl, &x, &y, text, font, 0xffffffff, kScreenWidth, kScreenHeight, 0, 0);
}

} // namespace Frigate::Game::Text
