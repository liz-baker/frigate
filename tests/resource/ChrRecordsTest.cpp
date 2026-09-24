#include "game/ChrRecords.h"

#include <cstdio>

using Frigate::Game::ChrRecords;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void TestParsesEverySlot() {
    auto records =
        ChrRecords::Parse("0\tBODY_Jungle_Commando\tCcamguardZ\t1\t0\n"
                          "1\tBODY_Xenia\tCxeniaZ\t0\t1\n"
                          "2\tBODY_Male_Pierce_Bond_1,HEAD_Male_Brosnan_Boiler\tCheadbrosnanboilerZ\t1\t1\n");
    Check(records != nullptr, "well-formed text parses");
    if (records == nullptr) {
        return;
    }

    const auto* commando = records->Find("BODY_Jungle_Commando");
    Check(commando != nullptr && commando->modelPath == "models/chr/CcamguardZ", "a slot is found by its name");
    Check(commando != nullptr && commando->isMale && !commando->hasHead, "with its flags");

    const auto* xenia = records->Find("BODY_Xenia");
    Check(xenia != nullptr && !xenia->isMale && xenia->hasHead, "a female body that carries her head");

    const auto* byBody = records->Find("BODY_Male_Pierce_Bond_1");
    const auto* byHead = records->Find("HEAD_Male_Brosnan_Boiler");
    Check(byBody != nullptr && byBody == byHead, "a slot with two names is found by either");

    Check(records->Find("BODY_Nobody") == nullptr, "an unknown name is not found");
}

void TestRejectsMalformedText() {
    Check(ChrRecords::Parse("0\tBODY_A\tCaZ\t1\n") == nullptr, "a line with too few fields is refused");
    Check(ChrRecords::Parse("1\tBODY_A\tCaZ\t1\t0\n") == nullptr, "slots must count up from 0");
    Check(ChrRecords::Parse("0\tBODY_A\tCaZ\tx\t0\n") == nullptr, "and flags must be numbers");
    Check(ChrRecords::Parse("") != nullptr, "empty text is an empty table");
}

} // namespace

int main() {
    TestParsesEverySlot();
    TestRejectsMalformedText();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All chr records tests passed\n");
    return 0;
}
