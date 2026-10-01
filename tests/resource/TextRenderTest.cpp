#include "game/TextRender.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Frigate;
using namespace Frigate::Game::Text;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// Every glyph 8 wide and 12 tall with its baseline 2 down, and a flat
// kerning table, so an advance is width + 1.
Font MakeFont() {
    Font font;
    font.Kerning.fill(0);
    for (size_t i = 0; i < Font::kNumChars; i++) {
        FontChar glyph;
        glyph.index = static_cast<int32_t>(i);
        glyph.baseline = 2;
        glyph.height = 12;
        glyph.width = 8;
        glyph.kerningIndex = 0;
        glyph.Pixels.assign(8 * 12, 0xff);
        font.Chars.push_back(glyph);
    }
    return font;
}

// Fixed cost of one drawn glyph: texture-LUT off, the seven-command load, one
// texture rectangle.
constexpr int kGlyphCommands = 1 + 7 + 3;

void TestMeasureSumsAdvancesAndLines() {
    Font font = MakeFont();
    Check(Measure(font, "AB").width == 18, "each glyph advances by width - (kerning - 1)");
    Check(Measure(font, "AB").height == 0, "a string with no newline has no measured height");
    Check(Measure(font, "AB\n").height == 14, "a newline adds the '[' glyph's height + baseline");
    Check(Measure(font, "A B").width == 9 + 5 + 9, "a space advances by 5");
    Check(Measure(font, " \n").width == 0, "a space before a newline adds no width");
    Check(Measure(font, "AAAA\nB").width == 36, "the widest line wins");
    Check(Measure(font, "A", 20).height == 0 && Measure(font, "A\n", 20).height == 20, "explicit line height");
}

void TestRenderEmitsOneRectanglePerGlyph() {
    Font font = MakeFont();
    Gfx buffer[64];
    int32_t x = 100;
    int32_t y = 50;
    Gfx* end = Render(buffer, &x, &y, "A B", font, 0xffffffff, 440, 330, 0, 0);
    Check(end - buffer == 1 + 2 * kGlyphCommands, "prim color, then a load and rectangle per non-space glyph");
    Check(x == 100 + 9 + 5 + 9, "x advances past the string");
    Check(y == 50, "y is unchanged without a newline");

    Gfx* rect = buffer + 1 + kGlyphCommands - 3;
    uint32_t xh = (rect->words.w0 >> 12) & 0xfff;
    uint32_t xl = (rect[0].words.w1 >> 12) & 0xfff;
    uint32_t yl = rect[0].words.w1 & 0xfff;
    Check(xl == (100 - (0 - 1)) * 4 && xh == xl + 8 * 4, "the rectangle covers the glyph's width at its kerned x");
    Check(yl == (50 + 2) * 4, "the rectangle starts at y + baseline");
}

void TestRenderSkipsGlyphsOffScreen() {
    Font font = MakeFont();
    Gfx buffer[64];
    int32_t x = 500;
    int32_t y = 50;
    Gfx* end = Render(buffer, &x, &y, "A", font, 0xffffffff, 440, 330, 0, 0);
    Check(end - buffer == 1, "a glyph past the right edge draws nothing");
    Check(x == 500 + 9, "but still advances");
}

void TestNewlineAdvancesYAndReturnsX() {
    Font font = MakeFont();
    Gfx buffer[64];
    int32_t x = 100;
    int32_t y = 50;
    Render(buffer, &x, &y, "A\nB", font, 0xffffffff, 440, 330, 0, 0);
    Check(y == 50 + 14, "newline moves down one line");
    Check(x == 100 + 9, "and x restarts from the origin");
}

void TestAlignedDrawOffsetsByMeasuredExtent() {
    Font font = MakeFont();
    Gfx left[64];
    Gfx centered[64];
    DrawAlignedWhite(left, 200, 100, kLeft, kLeft, "AB", font);
    DrawAlignedWhite(centered, 200, 100, kCenter, kCenter, "AB", font);

    // Command 0 is the prim color, then the LUT mode and the 7-command load precede the rectangle.
    auto xl = [](const Gfx* dl) { return (dl[1 + 8].words.w1 >> 12) & 0xfff; };
    Check(xl(left) == 201 * 4, "left/top alignment draws from the given point");
    Check(xl(centered) == (200 - 18 / 2 + 1) * 4, "center alignment shifts left by half the string's width");
}

// A glyph's texture rectangles among the commands, by their opcode (G_TEXRECT).
int CountRectangles(const Gfx* begin, const Gfx* end) {
    int count = 0;
    for (const Gfx* command = begin; command < end; command++) {
        if ((command->words.w0 >> 24) == static_cast<uint32_t>(G_TEXRECT & 0xff)) {
            count++;
        }
    }
    return count;
}

void TestOutlinedTextDrawsNineCopiesOfEachGlyph() {
    Font font = MakeFont();
    Gfx buffer[128];
    int32_t x = 100;
    int32_t y = 50;
    Gfx* end = RenderOutlined(buffer, &x, &y, "A", font, 0xffffffff, 0x646464ff, 440, 330, 0, 0);
    Check(CountRectangles(buffer, end) == 9, "a glyph is drawn eight times round itself and once over them");
    Check(x == 100 + 9, "and the text advances as plain text does");

    // The outline is in its own colour, the glyph in the text's.
    const uint32_t outline = buffer[8].words.w1;
    Check((buffer[8].words.w0 >> 24) == static_cast<uint32_t>(G_SETPRIMCOLOR & 0xff) && outline == 0x646464ff,
          "the outline is set as the primitive colour before its copies");

    // The copies sit a pixel either side of the glyph.
    std::vector<uint32_t> lefts;
    for (const Gfx* command = buffer; command < end; command++) {
        if ((command->words.w0 >> 24) == static_cast<uint32_t>(G_TEXRECT & 0xff)) {
            lefts.push_back((command->words.w1 >> 12) & 0xfff);
        }
    }
    Check(lefts.size() == 9 && *std::min_element(lefts.begin(), lefts.end()) == 100 * 4 &&
              *std::max_element(lefts.begin(), lefts.end()) == 102 * 4 && lefts.back() == 101 * 4,
          "the copies sit a pixel either side, and the glyph itself last");

    x = 500;
    Gfx* off = RenderOutlined(buffer, &x, &y, "A", font, 0xffffffff, 0x646464ff, 440, 330, 0, 0);
    Check(off == buffer, "a glyph past the edge draws nothing");
}

void TestBlackBoxAndRestore() {
    Gfx buffer[32];
    Gfx* end = DrawBlackBox(buffer, 10, 20, 110, 40);
    int fills = 0;
    for (const Gfx* command = buffer; command < end; command++) {
        fills += (command->words.w0 >> 24) == static_cast<uint32_t>(G_FILLRECT & 0xff) ? 1 : 0;
    }
    Check(fills == 1, "the black box is one fill");
    Check(RestoreRdp(buffer) - buffer == 4, "restoring the game's settings is a sync and three modes");
}

} // namespace

int main() {
    TestMeasureSumsAdvancesAndLines();
    TestRenderEmitsOneRectanglePerGlyph();
    TestRenderSkipsGlyphsOffScreen();
    TestNewlineAdvancesYAndReturnsX();
    TestAlignedDrawOffsetsByMeasuredExtent();
    TestOutlinedTextDrawsNineCopiesOfEachGlyph();
    TestBlackBoxAndRestore();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
