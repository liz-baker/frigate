#include "level/ScreenLayout.h"

#include <array>
#include <cstdio>

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    bool operator==(const Rect& other) const {
        return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
    }
};

void CheckView(const ViewMetrics& vi, int32_t left, int32_t top, int32_t width, int32_t height, const char* what) {
    const bool same = vi.viewLeft == left && vi.viewTop == top && vi.viewWidth == width && vi.viewHeight == height &&
                      vi.x == 320 && vi.y == 240;
    if (!same) {
        std::fprintf(stderr, "  %s: got %d,%d %dx%d, wanted %d,%d %dx%d\n", what, vi.viewLeft, vi.viewTop,
                     vi.viewWidth, vi.viewHeight, left, top, width, height);
    }
    Check(same, what);
}

void TestOnePlayerViews() {
    CheckView(PlayerViewMetrics(1, 0, ScreenSize::Full, 0.0f), 0, 10, 320, 220, "one player, full screen");
    CheckView(PlayerViewMetrics(1, 0, ScreenSize::Wide, 0.0f), 0, 30, 320, 180, "one player, wide screen");
    CheckView(PlayerViewMetrics(1, 0, ScreenSize::Cinema, 0.0f), 0, 52, 320, 136, "one player, cinema");
    // The arm coming up for the watch opens the letterbox out.
    CheckView(PlayerViewMetrics(1, 0, ScreenSize::Wide, 1.0f), 0, 10, 320, 220, "wide screen, arm up");
    CheckView(PlayerViewMetrics(1, 0, ScreenSize::Cinema, 1.0f), 0, 10, 320, 220, "cinema, arm up");
    CheckView(PlayerViewMetrics(1, 0, ScreenSize::Cinema, 0.5f), 0, 31, 320, 178, "cinema, arm halfway");
}

void TestTwoPlayerViews() {
    // Two players: the top and bottom halves, whatever the screen setting.
    for (const ScreenSize screen : { ScreenSize::Full, ScreenSize::Wide, ScreenSize::Cinema }) {
        CheckView(PlayerViewMetrics(2, 0, screen, 0.0f), 0, 10, 320, 109, "two players, player 1");
        CheckView(PlayerViewMetrics(2, 1, screen, 0.0f), 0, 121, 320, 109, "two players, player 2");
    }
    CheckView(PlayerViewMetrics(2, 1, ScreenSize::Cinema, 1.0f), 0, 121, 320, 109, "the watch does not open it");
}

void TestQuadrantViews() {
    for (const int count : { 3, 4 }) {
        CheckView(PlayerViewMetrics(count, 0, ScreenSize::Full, 0.0f), 0, 10, 159, 109, "quadrant, player 1");
        CheckView(PlayerViewMetrics(count, 1, ScreenSize::Full, 0.0f), 161, 10, 159, 109, "quadrant, player 2");
        CheckView(PlayerViewMetrics(count, 2, ScreenSize::Full, 0.0f), 0, 121, 159, 109, "quadrant, player 3");
    }
    CheckView(PlayerViewMetrics(4, 3, ScreenSize::Cinema, 0.5f), 161, 121, 159, 109, "quadrant, player 4");
}

void TestViewsStayOnScreen() {
    for (int count = 1; count <= 4; count++) {
        for (int player = 0; player < count; player++) {
            const ViewMetrics vi = PlayerViewMetrics(count, player, ScreenSize::Full, 0.0f);
            Check(vi.viewLeft >= 0 && vi.viewLeft + vi.viewWidth <= 320 && vi.viewTop >= 0 &&
                      vi.viewTop + vi.viewHeight <= 240,
                  "every view is inside the 320 by 240 screen");
            for (int other = 0; other < player; other++) {
                const ViewMetrics o = PlayerViewMetrics(count, other, ScreenSize::Full, 0.0f);
                const bool apart = vi.viewLeft + vi.viewWidth <= o.viewLeft || o.viewLeft + o.viewWidth <= vi.viewLeft ||
                                   vi.viewTop + vi.viewHeight <= o.viewTop || o.viewTop + o.viewHeight <= vi.viewTop;
                Check(apart, "no two views overlap");
            }
        }
    }
}

void TestZBufferAllocation() {
    Check(ZBufferAllocation(1, false).width == 320 && ZBufferAllocation(1, false).height == 240,
          "one player's z-buffer is the screen");
    for (const int count : { 2, 3, 4 }) {
        Check(ZBufferAllocation(count, false).width == 320 && ZBufferAllocation(count, false).height == 120,
              "a split screen's z-buffer is half the screen");
    }
    Check(ZBufferAllocation(1, true).width == 440 && ZBufferAllocation(1, true).height == 330,
          "the hi-res mode's z-buffer is 440 by 330 whatever the players");
    Check(ZBufferAllocation(2, true).width == 440 && ZBufferAllocation(2, true).height == 330, "hi-res, two players");
    Check(ScreenExtent(false).width == 320 && ScreenExtent(false).height == 240, "the screen is 320 by 240");
    Check(ScreenExtent(true).width == 440 && ScreenExtent(true).height == 330, "the hi-res screen is 440 by 330");
}

void TestZBufferOffsets() {
    Check(ZBufferRowOffset(1, 0) == 0, "one player's depth image is not moved");
    Check(ZBufferRowOffset(2, 0) == 0 && ZBufferRowOffset(2, 1) == 120, "the second of two views is the lower");
    Check(ZBufferRowOffset(3, 0) == 0 && ZBufferRowOffset(3, 1) == 0 && ZBufferRowOffset(3, 2) == 120,
          "the third of three is the lower left");
    Check(ZBufferRowOffset(4, 2) == 120 && ZBufferRowOffset(4, 3) == 120 && ZBufferRowOffset(4, 1) == 0,
          "the third and fourth of four are the lower pair");
}

void CheckClear(int count, int player, Rect want, const char* what) {
    const ZBufferRect got = ZBufferClearRect(count, player, false);
    const Rect gotRect{ got.left, got.top, got.right, got.bottom };
    if (!(gotRect == want)) {
        std::fprintf(stderr, "  %s: got %d,%d..%d,%d wanted %d,%d..%d,%d\n", what, gotRect.left, gotRect.top,
                     gotRect.right, gotRect.bottom, want.left, want.top, want.right, want.bottom);
    }
    Check(gotRect == want, what);
}

void TestZBufferClear() {
    CheckClear(1, 0, { 0, 0, 319, 239 }, "one player clears the screen");
    CheckClear(2, 0, { 0, 0, 319, 119 }, "two players, upper");
    CheckClear(2, 1, { 0, 120, 319, 239 }, "two players, lower");
    CheckClear(3, 0, { 0, 0, 159, 119 }, "three players, upper left");
    CheckClear(3, 1, { 160, 0, 319, 119 }, "three players, upper right");
    CheckClear(3, 2, { 0, 120, 159, 239 }, "three players, lower left");
    CheckClear(4, 3, { 160, 120, 319, 239 }, "four players, lower right");
    const ZBufferRect hiRes = ZBufferClearRect(1, 0, true);
    Check(hiRes.left == 0 && hiRes.top == 0 && hiRes.right == 439 && hiRes.bottom == 329,
          "hi-res clears the 440 by 330 screen");

    // Every player's clear lands inside the views of the players it shares a
    // z-buffer half with and on nobody else's.
    for (int count = 2; count <= 4; count++) {
        for (int player = 0; player < count; player++) {
            const ViewMetrics vi = PlayerViewMetrics(count, player, ScreenSize::Full, 0.0f);
            const ZBufferRect clear = ZBufferClearRect(count, player, false);
            Check(clear.left <= vi.viewLeft && clear.right >= vi.viewLeft + vi.viewWidth - 1 && clear.top <= vi.viewTop &&
                      clear.bottom >= vi.viewTop + vi.viewHeight - 1,
                  "a player's z clear covers their view");
            for (int other = 0; other < count; other++) {
                if (other == player) {
                    continue;
                }
                const ViewMetrics o = PlayerViewMetrics(count, other, ScreenSize::Full, 0.0f);
                const bool touches = clear.left <= o.viewLeft + o.viewWidth - 1 && clear.right >= o.viewLeft &&
                                     clear.top <= o.viewTop + o.viewHeight - 1 && clear.bottom >= o.viewTop;
                Check(!touches, "a player's z clear leaves the other views' depth alone");
            }
        }
    }
}

} // namespace

int main() {
    TestOnePlayerViews();
    TestTwoPlayerViews();
    TestQuadrantViews();
    TestViewsStayOnScreen();
    TestZBufferAllocation();
    TestZBufferOffsets();
    TestZBufferClear();
    if (gFailures == 0) {
        std::printf("ScreenLayoutTest: all passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
