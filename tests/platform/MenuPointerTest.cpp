#include "platform/MenuPointer.h"

#include <cmath>
#include <cstdio>

#include "platform/InputState.h"

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 0.001f) {
    return std::fabs(a - b) < tolerance;
}

void TestMapping() {
    // A 4:3 window is the screen exactly.
    ScreenPoint p = MapToScreen(320.0f, 240.0f, 640.0f, 480.0f);
    Check(Near(p.u, 0.5f) && Near(p.v, 0.5f), "the middle of a 4:3 window");
    p = MapToScreen(0.0f, 0.0f, 640.0f, 480.0f);
    Check(Near(p.u, 0.0f) && Near(p.v, 0.0f), "its top left");
    p = MapToScreen(640.0f, 480.0f, 640.0f, 480.0f);
    Check(Near(p.u, 1.0f) && Near(p.v, 1.0f), "its bottom right");

    // 16:9 at 1920x1080: the 4:3 screen is 1440 wide with 240 pixel bars.
    p = MapToScreen(240.0f, 0.0f, 1920.0f, 1080.0f);
    Check(Near(p.u, 0.0f) && Near(p.v, 0.0f), "the pillarbox edge is the screen's left");
    p = MapToScreen(1680.0f, 1080.0f, 1920.0f, 1080.0f);
    Check(Near(p.u, 1.0f) && Near(p.v, 1.0f), "and its right edge");
    p = MapToScreen(960.0f, 540.0f, 1920.0f, 1080.0f);
    Check(Near(p.u, 0.5f) && Near(p.v, 0.5f), "the middle");
    p = MapToScreen(240.0f + 360.0f, 270.0f, 1920.0f, 1080.0f);
    Check(Near(p.u, 0.25f) && Near(p.v, 0.25f), "a quarter in");
    p = MapToScreen(100.0f, 540.0f, 1920.0f, 1080.0f);
    Check(Near(p.u, 0.0f) && Near(p.v, 0.5f), "a point in the left bar clamps to the edge");
    p = MapToScreen(1900.0f, 540.0f, 1920.0f, 1080.0f);
    Check(Near(p.u, 1.0f), "and the right bar");

    // Ultrawide 21:9 (2560x1080): the screen is 1440 wide, 560 pixel bars.
    p = MapToScreen(560.0f + 720.0f, 540.0f, 2560.0f, 1080.0f);
    Check(Near(p.u, 0.5f) && Near(p.v, 0.5f), "ultrawide middle");
    p = MapToScreen(560.0f + 360.0f, 810.0f, 2560.0f, 1080.0f);
    Check(Near(p.u, 0.25f) && Near(p.v, 0.75f), "ultrawide quarter and three quarters");

    // Narrower than 4:3 (5:4, 1280x1024): letterboxed, the screen is 960 tall,
    // 32 pixel bars above and below.
    p = MapToScreen(640.0f, 512.0f, 1280.0f, 1024.0f);
    Check(Near(p.u, 0.5f) && Near(p.v, 0.5f), "5:4 middle");
    p = MapToScreen(0.0f, 32.0f, 1280.0f, 1024.0f);
    Check(Near(p.u, 0.0f) && Near(p.v, 0.0f), "5:4 top edge of the screen");
    p = MapToScreen(100.0f, 5.0f, 1280.0f, 1024.0f);
    Check(Near(p.v, 0.0f), "5:4 top bar clamps");
    // Portrait window.
    p = MapToScreen(300.0f, 600.0f, 600.0f, 1200.0f);
    Check(Near(p.u, 0.5f) && Near(p.v, 0.5f), "portrait middle");

    p = MapToScreen(10.0f, 10.0f, 0.0f, 0.0f);
    Check(Near(p.u, 0.0f) && Near(p.v, 0.0f), "an empty area maps to the origin");
}

PointerFrame At(float x, float y) {
    return { true, x, y, 640.0f, 480.0f };
}

void TestArbitration() {
    MenuPointerTracker tracker;
    PointerSample s = tracker.Update(At(100.0f, 100.0f), false);
    Check(!s.active, "a pointer just seen doesn't take the cursor");
    s = tracker.Update(At(100.0f, 100.0f), false);
    Check(!s.active, "nor does one that sits still");
    s = tracker.Update(At(160.0f, 240.0f), false);
    Check(s.active && Near(s.u, 0.25f) && Near(s.v, 0.5f), "moving it takes over, at its position");
    s = tracker.Update(At(160.0f, 240.0f), false);
    Check(s.active && Near(s.u, 0.25f), "and keeps the cursor while still");

    s = tracker.Update(At(160.0f, 240.0f), true);
    Check(!s.active, "a direction key gives the cursor back");
    s = tracker.Update(At(160.0f, 240.0f), false);
    Check(!s.active, "and the still pointer doesn't take it again");
    s = tracker.Update(At(161.0f, 240.0f), false);
    Check(s.active, "until it moves");

    s = tracker.Update(At(200.0f, 240.0f), true);
    Check(!s.active, "a direction wins over a move in the same frame");
    s = tracker.Update(At(210.0f, 240.0f), false);
    Check(s.active, "the next move takes over again");

    // Out of the window or under the ImGui menu.
    s = tracker.Update({}, false);
    Check(!s.active, "no pointer, no cursor");
    s = tracker.Update(At(500.0f, 400.0f), false);
    Check(s.active, "it still holds the cursor when it comes back");
    s = tracker.Update({}, true);
    s = tracker.Update(At(500.0f, 400.0f), false);
    Check(!s.active, "but a direction pressed meanwhile keeps it, and the return doesn't count as a move");
    s = tracker.Update(At(501.0f, 400.0f), false);
    Check(s.active, "the next move takes it");

    tracker.Reset();
    s = tracker.Update(At(10.0f, 10.0f), false);
    Check(!s.active, "a reset forgets who held it");
}

void TestStateCarriesThePointer() {
    PortState port;
    PadSample sample;
    sample.pointer = { true, 0.5f, 0.25f };
    port = Advance(port, sample);
    Check(port.pointer.active && Near(port.pointer.u, 0.5f) && Near(port.pointer.v, 0.25f), "the port reads it");
    port = Advance(port, PadSample{});
    Check(!port.pointer.active, "and loses it when the sample has none");
}

void TestUsedFlag() {
    TakeMenuPointerUsed();
    Check(!TakeMenuPointerUsed(), "unused to begin with");
    NoteMenuPointerUsed();
    Check(TakeMenuPointerUsed(), "noted");
    Check(!TakeMenuPointerUsed(), "and taken once");
}

} // namespace

int main() {
    TestMapping();
    TestArbitration();
    TestStateCarriesThePointer();
    TestUsedFlag();
    return gFailures == 0 ? 0 : 1;
}
