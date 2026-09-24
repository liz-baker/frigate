#include "model/Random.h"

#include <cstdio>
#include <vector>

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

std::vector<uint32_t> Draw(uint32_t seed, int count) {
    RandomSetSeed(seed);
    std::vector<uint32_t> values;
    for (int i = 0; i < count; i++) {
        values.push_back(RandomGetNext());
    }
    return values;
}

void TestTheSeedDecidesTheSequence() {
    Check(Draw(1234, 8) == Draw(1234, 8), "the same seed gives the same numbers");
    Check(Draw(1234, 8) != Draw(1235, 8), "a different seed gives different ones");
    Check(Draw(1234, 8)[0] != Draw(1234, 8)[1], "and the numbers change from one draw to the next");
}

} // namespace

int main() {
    TestTheSeedDecidesTheSequence();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All random tests passed\n");
    return 0;
}
