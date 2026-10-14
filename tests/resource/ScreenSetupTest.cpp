#include "level/ScreenSetup.h"

#include <array>
#include <cstdio>
#include <vector>

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

// The rectangles of the fill commands in a display list, in order.
std::vector<Rect> FillRects(const std::vector<Gfx>& list, const Gfx* end) {
    std::vector<Rect> rects;
    for (const Gfx* at = list.data(); at != end; at++) {
        if ((at->words.w0 >> 24) == static_cast<uint8_t>(G_FILLRECT)) {
            rects.push_back({ static_cast<int32_t>((at->words.w1 >> 14) & 0x3FF),
                              static_cast<int32_t>((at->words.w1 >> 2) & 0x3FF),
                              static_cast<int32_t>((at->words.w0 >> 14) & 0x3FF),
                              static_cast<int32_t>((at->words.w0 >> 2) & 0x3FF) });
        }
    }
    return rects;
}

void TestZBufferCommands() {
    std::vector<Gfx> list(64);
    Gfx* end = ClearPlayerZBuffer(list.data(), 4, 3, false, false);
    const std::vector<Rect> fills = FillRects(list, end);
    Check(fills.size() == 1, "one fill clears the player's depth");
    Check(fills.size() == 1 && fills[0] == Rect{ 160, 120, 319, 239 }, "the fourth player clears the lower right");
    int depthImages = 0;
    int colorImages = 0;
    uintptr_t depthAddress = 0;
    uintptr_t colorAddress = 0;
    for (const Gfx* at = list.data(); at != end; at++) {
        const uint32_t op = at->words.w0 >> 24;
        if (op == static_cast<uint8_t>(G_SETZIMG)) {
            depthImages++;
            depthAddress = at->words.w1;
        } else if (op == static_cast<uint8_t>(G_SETCIMG)) {
            colorImages++;
            colorAddress = at->words.w1;
        }
    }
    Check(depthImages == 1 && colorImages == 1, "one depth image and one color image are set");
    // The interpreter takes a fill into the depth image's address as a depth
    // clear.
    Check(depthAddress == colorAddress && depthAddress == kZBufferAddress, "the color image is the depth image");

    Gfx* back = SelectFramebuffer(list.data(), false);
    Check(back - list.data() == 1 && list[0].words.w1 == kFramebufferAddress, "the framebuffer is selected again");
}

// The fills of viSetupScreensForNumPlayers.
std::vector<Rect> Bars(int count, int player, int position) {
    std::vector<Gfx> list(64);
    const ViewMetrics vi = PlayerViewMetrics(count, player, ScreenSize::Full, 0.0f);
    Gfx* end = FillUncoveredScreen(list.data(), vi, count, position);
    return FillRects(list, end);
}

void CheckBars(const std::vector<Rect>& got, const std::vector<Rect>& want, const char* what) {
    bool same = got.size() == want.size();
    for (size_t i = 0; same && i < got.size(); i++) {
        same = got[i] == want[i];
    }
    if (!same) {
        std::fprintf(stderr, "  %s:\n", what);
        for (const Rect& r : got) {
            std::fprintf(stderr, "    got    %d,%d..%d,%d\n", r.left, r.top, r.right, r.bottom);
        }
        for (const Rect& r : want) {
            std::fprintf(stderr, "    wanted %d,%d..%d,%d\n", r.left, r.top, r.right, r.bottom);
        }
    }
    Check(same, what);
}

void TestBlackBars() {
    CheckBars(Bars(1, 0, 0), { { 0, 0, 319, 9 }, { 0, 230, 319, 239 } }, "one player: the rows above and below");
    // The first player in the draw order paints the bars the views share.
    CheckBars(Bars(2, 0, 0), { { 0, 0, 319, 9 }, { 0, 228, 319, 239 }, { 0, 119, 319, 120 } },
              "two players: the top, bottom and the gap");
    CheckBars(Bars(2, 1, 1), {}, "two players: the others paint nothing");
    CheckBars(Bars(2, 0, 1), {}, "two players: a first player drawn second paints nothing");
    CheckBars(
        Bars(3, 0, 0),
        { { 0, 0, 319, 9 }, { 0, 228, 319, 239 }, { 0, 119, 319, 120 }, { 159, 0, 160, 239 }, { 161, 121, 319, 239 } },
        "three players: the empty quadrant is black");
    CheckBars(Bars(4, 0, 0), { { 0, 0, 319, 9 }, { 0, 228, 319, 239 }, { 0, 119, 319, 120 }, { 159, 0, 160, 239 } },
              "four players: the cross between the views");
    CheckBars(Bars(4, 3, 3), {}, "four players: the last drawn paints nothing");
    // Drawn first, a lower view's own top is the rows above it, which the
    // views drawn after it cover.
    CheckBars(Bars(4, 2, 0), { { 0, 0, 319, 120 }, { 0, 119, 319, 120 }, { 159, 0, 160, 239 } },
              "four players: a lower view drawn first");
}

} // namespace

int main() {
    TestZBufferCommands();
    TestBlackBars();
    if (gFailures == 0) {
        std::printf("ScreenSetupTest: all passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
