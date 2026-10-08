#include "gfx/Rect2D.h"

#include <array>
#include <cmath>
#include <cstdio>

#include "level/ScreenLayout.h"

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// How far across a display `widen` times wider than 4:3 Fast3D puts a column of
// the 320 column screen's 2D: x is squeezed by 1 / widen about the middle of
// the framebuffer, whatever view it is drawn in.
float DisplayColumn(float column, float widen) {
    return ((((column / 160.0f) - 1.0f) / widen) + 1.0f) / 2.0f;
}

// The share of the display a view's 3D is stretched across: its columns of 320.
struct Span {
    float left;
    float right;
};
Span ViewSpan(const Level::ViewMetrics& vi) {
    return { static_cast<float>(vi.viewLeft) / 320.0f, static_cast<float>(vi.viewLeft + vi.viewWidth) / 320.0f };
}

constexpr std::array<float, 5> kWidens = { 1.0f, (16.0f / 10.0f) / (4.0f / 3.0f), (16.0f / 9.0f) / (4.0f / 3.0f),
                                           (21.0f / 9.0f) / (4.0f / 3.0f), (32.0f / 9.0f) / (4.0f / 3.0f) };

void TestAViewTheWidthOfTheScreenIsNotMoved() {
    for (const float widen : kWidens) {
        Check(Display::ViewShift2D(0, 320, 320, widen) == 0, "a full-width view's 2D stays where it is");
    }
    Check(Display::ViewShift2D(0, 159, 320, 1.0f) == 0 && Display::ViewShift2D(161, 159, 320, 1.0f) == 0,
          "so does a quadrant's on a 4:3 display");
}

void TestAQuadrantsMiddleLandsInItsView() {
    for (int players = 3; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            const Level::ViewMetrics vi = Level::PlayerViewMetrics(players, player, Level::ScreenSize::Full, 0.0f);
            const Span span = ViewSpan(vi);
            for (const float widen : kWidens) {
                const int32_t shift = Display::ViewShift2D(vi.viewLeft, vi.viewWidth, vi.x, widen);
                const float middle = static_cast<float>(vi.viewLeft) + (static_cast<float>(vi.viewWidth) * 0.5f);
                const float at = DisplayColumn(middle + static_cast<float>(shift), widen);
                const float want = (span.left + span.right) * 0.5f;
                Check(std::fabs(at - want) < 0.004f, "the middle of a quadrant's 2D is the middle of its view");
                // The whole 2D layer, which is the view's columns squeezed by the
                // widening, sits inside the view.
                const float left = DisplayColumn(static_cast<float>(vi.viewLeft + shift), widen);
                const float right = DisplayColumn(static_cast<float>(vi.viewLeft + vi.viewWidth + shift), widen);
                Check(left >= span.left - 0.004f && right <= span.right + 0.004f, "and its edges stay inside the view");
            }
        }
    }
}

void TestMovedRectanglesUseTheSignedCommands() {
    std::array<Gfx, 8> list{};
    {
        const Display::Shift2DScope scope(0);
        Gfx* end = Display::FillRectangle(list.data(), 10, 20, 30, 40);
        Check(end == list.data() + 1 && (list[0].words.w0 >> 24) == static_cast<uint8_t>(G_FILLRECT),
              "a rectangle that is not moved is the plain fill");
        end = Display::TextureRectangle(list.data(), 40, 80, 120, 160, 0, 0, 0x400, 0x400);
        Check(end == list.data() + 3 && (list[0].words.w0 >> 24) == static_cast<uint8_t>(G_TEXRECT),
              "and the plain texture rectangle");
    }
    {
        const Display::Shift2DScope scope(-27);
        Check(Display::Shift2D() == -27, "the scope sets the move");
        Gfx* end = Display::FillRectangle(list.data(), 10, 20, 30, 40);
        Check(end == list.data() + 2 && (list[0].words.w0 >> 24) == static_cast<uint8_t>(G_FILLWIDERECT),
              "a moved fill is the signed one");
        const int32_t right = static_cast<int32_t>(list[0].words.w0 << 8) >> 10;
        const int32_t left = static_cast<int32_t>(list[1].words.w0 << 8) >> 10;
        Check(left == -17 && right == 3, "with the columns moved");
        end = Display::TextureRectangle(list.data(), 40, 80, 120, 160, 0, 0, 0x400, 0x400);
        Check(end == list.data() + 3 && (list[0].words.w0 >> 24) == static_cast<uint8_t>(G_TEXRECT_WIDE),
              "a moved texture rectangle is the signed one");
        const int32_t xl = static_cast<int32_t>(list[1].words.w0 << 8) >> 8;
        const int32_t xh = static_cast<int32_t>(list[0].words.w0 << 8) >> 8;
        Check(xl == 40 - 108 && xh == 120 - 108, "with the quarter columns moved, past column 0 included");
    }
    Check(Display::Shift2D() == 0, "the move ends with the scope");

    // A rectangle with a negative column cannot be the plain command.
    Gfx* end = Display::FillRectangle(list.data(), -5, 0, 10, 4);
    Check(end == list.data() + 2 && (list[0].words.w0 >> 24) == static_cast<uint8_t>(G_FILLWIDERECT),
          "a negative column is the signed fill, moved or not");
}

} // namespace

int main() {
    TestAViewTheWidthOfTheScreenIsNotMoved();
    TestAQuadrantsMiddleLandsInItsView();
    TestMovedRectanglesUseTheSignedCommands();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::puts("Rect2DTest passed");
    return 0;
}
