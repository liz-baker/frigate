#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "libultraship/libultra/gbi.h"
#include "resource/type/Font.h"

namespace Frigate::Game::Text {

enum Alignment : int32_t { kLeft = 0, kCenter = 1, kRight = 2 };

struct Extent {
    int32_t width = 0;
    int32_t height = 0;
};

// textMeasure(). `lineHeight` 0 derives it from the '[' glyph, as the decomp does.
Extent Measure(const Font& font, std::string_view text, int32_t lineHeight = 0);

// microcode_constructor(): the RDP state every glyph draw assumes.
Gfx* SetupRdp(Gfx* dl);

// microcode_constructor_related_to_menus(): a filled rectangle of `colour`
// (0xRRGGBBAA) under translucent blending, then the combiner back to the
// text's. The cast screen draws one, fully transparent, behind each line.
Gfx* FillRect(Gfx* dl, int32_t left, int32_t top, int32_t right, int32_t bottom, uint32_t colour);

// textRender(): draws `text` from (*x, *y), advancing *x/*y past it, clipped to
// the rect (*x, *y) .. (*x + width, *y + height) as sampled on entry.
// `colour` is 0xRRGGBBAA. Glyph pixels are addressed in place, so `font` must
// outlive the display list.
//
// TODO(https://github.com/liz-baker/frigate/issues/89): the multi-byte
// (Japanese) glyph path and the text_x/y/s/t style globals; every one is at its
// default here.
Gfx* Render(Gfx* dl, int32_t* x, int32_t* y, std::string_view text, const Font& font, uint32_t colour, int32_t width,
            int32_t height, int32_t yOffset, int32_t lineHeight);

// setTextSpacingInverted(): glyphs sit one pixel further apart in Measure and
// Render until it is turned off again.
void SetSpacingInverted(bool inverted);

// setTextOrientation(): kRotated90Clockwise is the front-end's sideways tab
// text. Render takes the line position in the y coordinate and the glyph
// advance in x, as the decomp's callers do; RenderOutlined stays upright.
enum class Orientation : int32_t { kNormal = 0, kRotated90Clockwise = 1 };
void SetOrientation(Orientation orientation);

// setTextOverlapCorrection(): with a value of 0 or more, textRender moves down
// by it rather than by the line height at a line break that follows no
// drawing on its line. -1, the default, turns it off.
void SetOverlapCorrection(int32_t value);

// textWrap(): `text` with line breaks added where a word would run past
// `wrapWidth` pixels. The line indent (text_wordwrap) is its default of none.
std::string Wrap(int32_t wrapWidth, std::string_view text, const Font& font);

// textRenderOutlined(): as Render with `outlineColour` around each glyph.
// `font` must outlive the display list.
Gfx* RenderOutlined(Gfx* dl, int32_t* x, int32_t* y, std::string_view text, const Font& font, uint32_t colour,
                    uint32_t outlineColour, int32_t width, int32_t height, int32_t yOffset, int32_t lineHeight);

// draw_blackbox_to_screen(): a fully transparent rectangle, then the combiner
// back to the text's.
Gfx* DrawBlackBox(Gfx* dl, int32_t left, int32_t top, int32_t right, int32_t bottom);

// combiner_bayer_lod_perspective(): the dither, perspective and LOD settings
// the game draws with, which SetupRdp turned off.
Gfx* RestoreRdp(Gfx* dl);

// display_aligned_white_text_to_screen(): `halign`/`valign` are Alignment
// values; the string's measured extent is offset from (x, y) by that fraction.
Gfx* DrawAlignedWhite(Gfx* dl, int32_t x, int32_t y, int32_t halign, int32_t valign, std::string_view text,
                      const Font& font);

} // namespace Frigate::Game::Text
