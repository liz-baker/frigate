#include "platform/InputScript.h"

#include <cstdio>

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr uint16_t kA = 0x8000;
constexpr uint16_t kStart = 0x1000;
constexpr uint16_t kPadDown = 0x0400;

} // namespace

int main() {
    // A single frame holds until the next entry; a range releases after itself.
    {
        const auto script = InputScript::Parse("10:A,20-22:START+DD+X=-70+Y=5,40:");
        Check(script.has_value(), "valid script parses");
        Check(script->At(0).buttons == 0, "nothing before the first entry");
        Check(script->At(9).buttons == 0, "nothing the frame before");
        Check(script->At(10).buttons == kA, "entry starts on its frame");
        Check(script->At(19).buttons == kA, "single frame entry holds");
        const PadSample s = script->At(21);
        Check(s.buttons == (kStart | kPadDown), "buttons join");
        Check(s.stickX == -70 && s.stickY == 5, "stick values");
        Check(script->At(22).buttons == (kStart | kPadDown), "range includes its last frame");
        Check(script->At(23).buttons == 0 && script->At(23).stickX == 0, "range releases after");
        Check(script->At(1000).buttons == 0, "empty entry releases");
    }
    // An entry on the frame a range ends on is not displaced by the release.
    {
        const auto script = InputScript::Parse("5-6:A,7:START");
        Check(script->At(7).buttons == kStart, "entry after a range wins the release frame");
    }
    Check(InputScript::Parse("").has_value(), "empty script is valid");
    Check(InputScript::Parse("")->At(3).buttons == 0, "empty script reads zero");
    for (const char* bad : { "10", "x:A", "10:Q", "5-3:A", "10:X=", "10:X=999", "-1:A", "10:A+" }) {
        std::string error;
        Check(!InputScript::Parse(bad, &error).has_value() && !error.empty(), bad);
    }
    return gFailures == 0 ? 0 : 1;
}
