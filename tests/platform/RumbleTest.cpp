#include "platform/Rumble.h"

#include <cstdio>
#include <vector>

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

} // namespace

int main() {
    std::vector<std::pair<int, bool>> calls;
    const Rumble::Motor motor = [&calls](int port, bool on) {
        calls.emplace_back(port, on);
        return true;
    };

    Rumble rumble;
    rumble.Start(0, 0.25f);
    rumble.Tick(motor);
    Check(calls.empty() && rumble.Timer(0) == 0, "a port that is not ready ignores the start");

    rumble.SetReady(1);
    rumble.Start(0, 0.1f);
    Check(rumble.Timer(0) == 6, "0.1 seconds is 6 ticks");
    rumble.Start(0, 0.05f);
    Check(rumble.Timer(0) == 6, "a shorter start does not shorten the rumble");
    rumble.Start(0, 0.25f);
    Check(rumble.Timer(0) == 15, "a longer one extends it");

    rumble.Tick(motor);
    Check(calls.size() == 1 && calls[0].first == 0 && calls[0].second && rumble.Running(0),
          "the first tick starts the motor");
    Check(rumble.Timer(0) == 14, "and counts down");

    for (int i = 0; i < 13; i++) {
        rumble.Tick(motor);
    }
    Check(rumble.Timer(0) == 1 && calls.size() == 1, "the motor keeps running while the timer does");
    rumble.Tick(motor);
    Check(rumble.Timer(0) == 0 && rumble.Running(0), "the timer runs out");
    rumble.Tick(motor);
    Check(calls.size() == 2 && !calls[1].second && !rumble.Running(0), "and the next tick stops the motor");

    rumble.Start(0, 0.1f);
    rumble.Tick(motor);
    Check(rumble.Running(0), "a new start runs it again");

    Rumble failing;
    failing.SetReady(2);
    failing.Start(1, 0.1f);
    failing.Tick([](int, bool) { return false; });
    failing.Start(1, 0.1f);
    Check(failing.Timer(1) == 5, "a motor that does not answer takes the port out");

    return gFailures == 0 ? 0 : 1;
}
