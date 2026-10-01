#include <cstdio>

#include "level/Shading.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

StanTile TileWithColour(uint16_t mid) {
    StanTile tile;
    tile.mid = mid;
    return tile;
}

void TestTileRgbDoublesNibbles() {
    const auto rgb = TileRgb(TileWithColour(0x0840));
    Check(rgb[0] == 0x88 && rgb[1] == 0x44 && rgb[2] == 0x00, "each nibble fills a byte");
    Check(TileRgb(TileWithColour(0xFABC))[0] == 0xAA, "the special kind above the colour is not part of it");
}

void TestShadeFromTile() {
    // (0x88, 0x44, 0x00): brightness (136 * 79 + 68 * 156) >> 8 = 83, so an
    // alpha of (255 - 83) * 0.75 = 129; the hue at full saturation is
    // (136, 68, 0), halved.
    const ShadeColour shade = ShadeFromTile(TileWithColour(0x0840));
    Check(shade[0] == 68 && shade[1] == 34 && shade[2] == 0 && shade[3] == 129, "a brown floor");

    const ShadeColour black = ShadeFromTile(TileWithColour(0x0000));
    Check(black[0] == 0 && black[1] == 0 && black[2] == 0 && black[3] == 191, "a black floor is all alpha");

    // A grey floor has no hue: the range from its darkest to its brightest
    // channel is empty.
    const ShadeColour grey = ShadeFromTile(TileWithColour(0x0888));
    Check(grey[0] == 0 && grey[1] == 0 && grey[2] == 0, "a grey floor tints nothing");

    // The ordering of the channels does not change what comes out.
    const ShadeColour blue = ShadeFromTile(TileWithColour(0x0048));
    Check(blue[0] == 0 && blue[1] == 34 && blue[2] == 68, "a blue-green floor");
}

void TestObjectShadeIsHalvedAgain() {
    const ShadeColour shade = ObjectShadeFromTile(TileWithColour(0x0840));
    Check(shade[0] == 34 && shade[1] == 17 && shade[2] == 0 && shade[3] == 129, "the alpha is not");
}

void TestEaseShading() {
    ShadeColour shade = { 0, 0, 0, 0 };
    EaseShading(shade, { 64, 8, 1, 255 });
    // An eighth of the way, rounded up: (difference + 7) >> 3.
    Check(shade[0] == 8 && shade[1] == 1 && shade[2] == 1 && shade[3] == 32, "towards a brighter tile");
    EaseShading(shade, { 0, 0, 0, 0 });
    Check(shade[0] == 7 && shade[1] == 1 && shade[2] == 1 && shade[3] == 28, "and back down");

    ShadeColour settled = { 10, 10, 10, 10 };
    EaseShading(settled, { 10, 10, 10, 10 });
    Check(settled[0] == 10 && settled[3] == 10, "a settled shading stays");
}

} // namespace

int main() {
    TestTileRgbDoublesNibbles();
    TestShadeFromTile();
    TestObjectShadeIsHalvedAgain();
    TestEaseShading();
    return gFailures == 0 ? 0 : 1;
}
