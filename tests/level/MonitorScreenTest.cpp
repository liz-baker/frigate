#include <cmath>
#include <cstdio>
#include <string>

#include "level/MonitorScreen.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) < tolerance;
}

// Two lists: `hold` holds twenty frames and loops; `chance` jumps to `hold`
// when the draw's high half-word is below 0x4000, otherwise yields.
const char* kLists = "list\thold\t7\t3\t8\t20\t11\n"
                     "list\tchance\t10\t@hold\t16384\t12\n"
                     "list\tscroll\t1\t1024\t20\t8\t20\t11\n"
                     "list\tcolour\t13\t4278190335\t10\t8\t20\t11\n"
                     "default\thold\n"
                     "image\t1\tchance\n"
                     "image\t2\tscroll\n"
                     "image\t3\tcolour\n";

void TestParse() {
    const auto lists = MonitorLists::Parse(kLists);
    Check(lists != nullptr, "the lists parse");
    Check(MonitorLists::Parse("list\tbad\t9\t@nowhere\n") == nullptr, "a reference to no list is refused");
    Check(MonitorLists::Parse("junk\n") == nullptr, "a line of no kind is refused");
    if (!lists) {
        return;
    }
    Check(lists->ListFor(0) == lists->ListNamed("hold"), "a number with no list gets the default");
    Check(lists->ListFor(1) == lists->ListNamed("chance"), "number 1 picks its list");
    Check(lists->ListFor(99) == lists->ListNamed("hold"), "a number past the table gets the default");
}

void TestHoldAndTexture() {
    const auto lists = MonitorLists::Parse(kLists);
    MonitorScreen screen = MonitorScreen::Make(*lists, 0);
    int draws = 0;
    const MonitorScreen::RandomFn random = [&draws] {
        draws++;
        return 0u;
    };

    screen.Advance(*lists, 1, 1.0f, random);
    Check(screen.image == 3, "the first command sets the image");
    Check(screen.pause == 20, "a hold starts its count");
    Check(draws == 0, "a list with no chance draws nothing");

    // 20 frames of hold at three ticks a frame.
    for (int frame = 0; frame < 6; frame++) {
        screen.Advance(*lists, 3, 3.0f, random);
    }
    Check(screen.pause == 2, "a hold counts down by the clock");
    screen.Advance(*lists, 3, 3.0f, random);
    Check(screen.pause == 20, "a hold that ends carries on, round the loop, into the next");
}

void TestChance() {
    const auto lists = MonitorLists::Parse(kLists);
    MonitorScreen screen = MonitorScreen::Make(*lists, 1);
    int draws = 0;
    uint32_t value = 0;
    const MonitorScreen::RandomFn random = [&] {
        draws++;
        return value;
    };

    screen.Advance(*lists, 1, 1.0f, random);
    Check(draws == 1, "a chance draws once");
    Check(screen.image == 3, "a draw below the threshold jumps to the other list");

    screen = MonitorScreen::Make(*lists, 1);
    draws = 0;
    value = 0x40000000u;
    screen.Advance(*lists, 1, 1.0f, random);
    Check(draws == 1, "a chance that fails draws once");
    Check(screen.image == 0, "a draw at the threshold does not jump");
}

void TestScroll() {
    const auto lists = MonitorLists::Parse(kLists);
    MonitorScreen screen = MonitorScreen::Make(*lists, 2);
    const MonitorScreen::RandomFn random = [] { return 0u; };
    screen.Advance(*lists, 1, 1.0f, random);
    // 1024 units is a whole image over 20 frames; one frame in, a twentieth of the way.
    Check(Near(screen.xmid, 0.5f + 1.0f / 20.0f), "a scroll moves by its rate");
    for (int i = 0; i < 19; i++) {
        screen.Advance(*lists, 1, 1.0f, random);
    }
    Check(Near(screen.xmid, 1.5f), "a scroll ends where it was told");
}

void TestColour() {
    const auto lists = MonitorLists::Parse(kLists);
    MonitorScreen screen = MonitorScreen::Make(*lists, 3);
    const MonitorScreen::RandomFn random = [] { return 0u; };
    // Colour 0xFF0000FF, over ten frames, from white.
    for (int i = 0; i < 20; i++) {
        screen.Advance(*lists, 1, 1.0f, random);
    }
    Check(screen.red == 0xFF && screen.green == 0 && screen.blue == 0 && screen.alpha == 0xFF,
          "a colour change ends on its colour");
}

void TestStop() {
    const auto lists = MonitorLists::Parse("list\tstop\t12\n"
                                           "default\tstop\n");
    MonitorScreen screen = MonitorScreen::Make(*lists, 0);
    const MonitorScreen::RandomFn random = [] { return 0u; };
    screen.Advance(*lists, 1, 1.0f, random);
    Check(screen.offset == 0, "a list that stops stays where it is");
}

} // namespace

int main() {
    TestParse();
    TestHoldAndTexture();
    TestChance();
    TestScroll();
    TestColour();
    TestStop();
    if (gFailures == 0) {
        std::printf("MonitorScreenTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
