// TickMeter: the ticks a live stage frame lasts, from the time between frames.
#include <chrono>
#include <cstdio>

#include "game/FrameTiming.h"

namespace {

using Frigate::Game::TickMeter;
using std::chrono::microseconds;
using std::chrono::milliseconds;

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        failures++;
    }
}

} // namespace

int main() {
    const TickMeter::Clock::time_point start{};

    // The first frame, and the first after a reset, is one tick whenever it happens.
    {
        TickMeter meter;
        Check(meter.Ticks(start) == 1, "first frame is one tick");
        Check(meter.Ticks(start + milliseconds(34)) == 2, "34 ms is two ticks");
        meter.Reset();
        Check(meter.Ticks(start + milliseconds(900)) == 1, "first frame after a reset is one tick");
    }

    // The frame times a 60 Hz display produces, with the jitter they have.
    {
        TickMeter meter;
        auto now = start;
        meter.Ticks(now);
        struct Case {
            int micros;
            int ticks;
        };
        const Case cases[] = {
            { 16667, 1 }, { 16000, 1 }, { 17500, 1 }, { 33333, 2 }, { 34300, 2 },
            { 32000, 2 }, { 50000, 3 }, { 51000, 3 }, { 66667, 4 },
        };
        for (const Case& c : cases) {
            now += microseconds(c.micros);
            const int ticks = meter.Ticks(now);
            if (ticks != c.ticks) {
                std::fprintf(stderr, "FAILED: %d us gave %d ticks, wanted %d\n", c.micros, ticks, c.ticks);
                failures++;
            }
        }
    }

    // Faster than 60 frames a second still steps a tick, and a stall is capped.
    {
        TickMeter meter;
        meter.Ticks(start);
        Check(meter.Ticks(start + milliseconds(5)) == 1, "a quick frame is still one tick");
        Check(meter.Ticks(start + milliseconds(5) + milliseconds(250)) == TickMeter::kMaxTicks,
              "a stall is capped at kMaxTicks");
    }

    if (failures == 0) {
        std::puts("TickMeterTest: ok");
    }
    return failures == 0 ? 0 : 1;
}
