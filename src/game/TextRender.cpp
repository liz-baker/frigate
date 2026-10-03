#include "game/TextRender.h"

#include <algorithm>

#include "game/ScreenSize.h"
#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Text {

namespace {

constexpr int32_t kSpaceWidth = 5;
constexpr int32_t kFirstGlyph = 0x21; // '!'; the bank holds kNumChars glyphs from here

int32_t g_overlapCorrection = -1;
// text_spacing and text_orientation.
int32_t g_spacing = 0;
int32_t g_orientation = 0;

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
    return font.Kerning[previous.kerningIndex * 13 + current.kerningIndex] + g_spacing;
}

// The 8-bit intensity texture load both glyph renderers begin with; glyph
// indices of 0x80 and above are the paletted Japanese path (see the TODO in
// TextRender.h).
Gfx* LoadGlyph(Gfx* dl, const FontChar& current) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    // gDPLoadTextureBlock() with the image's real row stride in the SETTIMG
    // width: the interpreter takes its row stride from it, and the macro's
    // width of 1 makes it read the glyph as one long row.
    const int32_t stride = FontChar::RowStride(current.width);
    const int32_t line = stride / 8;
    gDPSetTextureImage(dl++, G_IM_FMT_I, G_IM_SIZ_8b, stride, const_cast<uint8_t*>(current.Pixels.data()));
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0,
               G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0);
    gDPLoadSync(dl++);
    gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, stride * current.height - 1, (2048 + line - 1) / line);
    gDPPipeSync(dl++);
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, line, 0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0,
               G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, 0);
    gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (stride - 1) << G_TEXTURE_IMAGE_FRAC,
                   (current.height - 1) << G_TEXTURE_IMAGE_FRAC);
    return dl;
}

// textRenderGlyph().
Gfx* RenderGlyph(Gfx* dl, int32_t* x, int32_t* y, const FontChar& current, const FontChar& previous, const Font& font,
                 int32_t clipX, int32_t clipY, int32_t clipWidth, int32_t clipHeight, int32_t yOffset) {
    using namespace Frigate::F3dex;

    int32_t drawY = *y + yOffset;
    *x -= Kerning(font, previous, current) - 1;

    int32_t top = current.baseline + drawY;
    int32_t bottom = top + current.height;

    bool onScreen = g_orientation != 0 || (*x > 0 && *x <= kScreenWidth && top <= kScreenHeight);
    if (onScreen && clipX + clipWidth >= *x && clipY + clipHeight >= top && *x >= clipX && bottom >= clipY) {
        dl = LoadGlyph(dl, current);

        if (*x + current.width <= clipX + clipWidth) {
            int32_t ulx = *x;
            int32_t lrx = *x + current.width;
            int32_t uly = top;
            int32_t lry = bottom;
            int32_t t = 0;
            bool draw = true;
            if (g_orientation != 0 && clipY <= top && bottom <= clipY + clipHeight) {
                // Turned a quarter clockwise: the glyph's rows run across the
                // screen, so the rectangle is flipped and its extent is in
                // y = *x, x = the line position minus the glyph's height.
                // TODO(https://github.com/liz-baker/libultraship-wiiu/issues/64): the
                // interpreter negates a flipped rectangle's increments and maps the
                // texture edge to edge, so the decomp's (0x400, 0xfc00) from row
                // height - 1 become (0xfc00, 0x400) from height.
                const int32_t lineEdge = drawY - current.baseline;
                gSPTextureRectangleFlip(dl++, (lineEdge - current.height) * 4, *x * 4, lineEdge * 4,
                                        (*x + current.width) * 4, G_TX_RENDERTILE, 0, current.height << 5, 0xfc00,
                                        0x400);
                draw = false;
            } else if (clipY > top) {
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

// textDrawGlyphQuad() with text_orientation == 0: the glyph's rectangle at
// (x, y), kept inside the clip rect.
Gfx* DrawGlyphQuad(Gfx* dl, int32_t x, int32_t y, const FontChar& current, int32_t clipX, int32_t clipY,
                   int32_t clipWidth, int32_t clipHeight) {
    using namespace Frigate::F3dex;
    if (clipX + clipWidth < current.width + x) {
        return dl;
    }
    const int32_t top = current.baseline + y;
    const int32_t bottom = top + current.height;
    if (top >= clipY) {
        if (clipY + clipHeight >= bottom) {
            gSPTextureRectangle(dl++, x * 4, top * 4, (x + current.width) * 4, bottom * 4, G_TX_RENDERTILE, 0, 0, 0x400,
                                0x400);
        } else if (clipY + clipHeight >= top) {
            gSPTextureRectangle(dl++, x * 4, top * 4, (x + current.width) * 4, (clipY + clipHeight) * 4,
                                G_TX_RENDERTILE, 0, 0, 0x400, 0x400);
        }
    } else if (bottom >= clipY) {
        gSPTextureRectangle(dl++, x * 4, clipY * 4, (x + current.width) * 4, bottom * 4, G_TX_RENDERTILE, 0,
                            (clipY - top) << 5, 0x400, 0x400);
    }
    return dl;
}

void SetPrimColour(Gfx*& dl, uint32_t colour) {
    gDPSetPrimColor(dl++, 0, 0, static_cast<uint8_t>(colour >> 24), static_cast<uint8_t>(colour >> 16),
                    static_cast<uint8_t>(colour >> 8), static_cast<uint8_t>(colour));
}

// textRenderGlyphOutlined().
Gfx* RenderGlyphOutlined(Gfx* dl, int32_t* x, int32_t* y, const FontChar& current, const FontChar& previous,
                         const Font& font, int32_t clipX, int32_t clipY, uint32_t textColour, uint32_t outlineColour,
                         int32_t clipWidth, int32_t clipHeight, int32_t yOffset) {
    const int32_t drawY = *y + yOffset;
    *x -= Kerning(font, previous, current) - 1;

    const int32_t top = drawY + current.baseline;
    if (*x > 0 && *x <= kScreenWidth && top <= kScreenHeight && *x <= clipX + clipWidth && top <= clipY + clipHeight &&
        *x >= clipX && top + current.height >= clipY) {
        dl = LoadGlyph(dl, current);

        // The outline is the glyph drawn at the eight offsets around it, then
        // the glyph itself over them.
        SetPrimColour(dl, outlineColour);
        for (int32_t offsetX = -1; offsetX < 2; offsetX++) {
            for (int32_t offsetY = -1; offsetY < 2; offsetY++) {
                if (offsetX != 0 || offsetY != 0) {
                    dl = DrawGlyphQuad(dl, *x + offsetX, drawY + offsetY, current, clipX, clipY, clipWidth, clipHeight);
                }
            }
        }
        SetPrimColour(dl, textColour);
        dl = DrawGlyphQuad(dl, *x, drawY, current, clipX, clipY, clipWidth, clipHeight);
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
            extent.width += (wide ? 15 : 11) - (font.Kerning[0] + g_spacing - 1);
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
            *y += g_overlapCorrection >= 0 && savedX == *x ? g_overlapCorrection : lineHeight;
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

void SetOverlapCorrection(int32_t value) {
    g_overlapCorrection = value;
}

void SetSpacingInverted(bool inverted) {
    g_spacing = inverted ? -1 : 0;
}

void SetOrientation(Orientation orientation) {
    g_orientation = static_cast<int32_t>(orientation);
}

std::string Wrap(int32_t wrapWidth, std::string_view text, const Font& font) {
    std::string out;
    size_t at = 0;
    int32_t lineWidth = 0;
    bool more = true;
    const auto charAt = [&text](size_t index) { return index < text.size() ? static_cast<uint8_t>(text[index]) : 0; };

    while (more) {
        std::string word;
        while (charAt(at) > ' ') {
            word += text[at++];
            // A two-byte glyph takes its trailing byte along.
            if (static_cast<uint8_t>(word.back()) >= 0x80) {
                word += static_cast<char>(charAt(at++));
            }
        }
        const int32_t wordWidth = Measure(font, word).width;
        lineWidth += wordWidth;
        const bool fits = lineWidth <= wrapWidth;

        const uint8_t next = charAt(at);
        if (next == '\n') {
            if (!fits) {
                out += '\n';
            }
            lineWidth = 0;
            out += word;
            out += '\n';
        } else if (next == ' ') {
            if (!fits) {
                out += '\n';
                lineWidth = wordWidth;
            }
            lineWidth += kSpaceWidth;
            out += word;
            out += ' ';
        } else if (next == 0) {
            more = false;
            if (!fits) {
                out += '\n';
            }
            out += word;
        }
        at++;
    }
    return out;
}

Gfx* RenderOutlined(Gfx* dl, int32_t* x, int32_t* y, std::string_view text, const Font& font, uint32_t colour,
                    uint32_t outlineColour, int32_t width, int32_t height, int32_t yOffset, int32_t lineHeight) {
    const int32_t savedX = *x;
    const int32_t savedY = *y;
    char previous = 'H';

    if (lineHeight == 0) {
        lineHeight = DefaultLineHeight(font);
    }

    for (size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (c == ' ') {
            *x += kSpaceWidth;
            previous = 'H';
        } else if (c == '\n') {
            previous = 'H';
            *x = savedX;
            *y += lineHeight;
        } else if (static_cast<uint8_t>(c) < 0x80) {
            const FontChar* current = Glyph(font, c);
            const FontChar* before = Glyph(font, previous);
            if (current == nullptr) {
                continue;
            }
            dl = RenderGlyphOutlined(dl, x, y, *current, *before, font, savedX, savedY, colour, outlineColour, width,
                                     height, yOffset);
            previous = c;
        } else {
            i++;
        }
    }
    return dl;
}

Gfx* DrawBlackBox(Gfx* dl, int32_t left, int32_t top, int32_t right, int32_t bottom) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, 0x00, 0x00, 0x00, 0x00);
    gDPFillRectangle(dl++, left, top, right, bottom);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineLERP(dl++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    return dl;
}

Gfx* RestoreRdp(Gfx* dl) {
    using namespace Frigate::F3dex;
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_LOD);
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
