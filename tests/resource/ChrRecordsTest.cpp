#include "game/ChrRecords.h"

#include <cstdio>

using Frigate::Game::ChrHats;
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
    auto records = ChrRecords::Parse(
        "0\tBODY_Jungle_Commando\tCcamguardZ\t1\t0\t1.0\t1.0\t100.0\n"
        "1\tBODY_Xenia\tCxeniaZ\t0\t1\t1.0\t1.0\t100.0\n"
        "2\tBODY_Male_Pierce_Bond_1,HEAD_Male_Brosnan_Boiler\tCheadbrosnanboilerZ\t1\t1\t1.0\t1.0\t100.0\n");
    Check(records != nullptr, "well-formed text parses");
    if (records == nullptr) {
        return;
    }

    const auto* fields = records->Find("BODY_Xenia");
    Check(fields != nullptr && fields->scale == 1.0f && fields->pov == 1.0f && fields->radius == 100.0f,
          "a slot's scale, animation translation scale and radius are read");

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
    Check(ChrRecords::Parse("1\tBODY_A\tCaZ\t1\t0\t1.0\t1.0\t100.0\n") == nullptr, "slots must count up from 0");
    Check(ChrRecords::Parse("0\tBODY_A\tCaZ\tx\t0\t1.0\t1.0\t100.0\n") == nullptr, "and flags must be numbers");
    Check(ChrRecords::Parse("") != nullptr, "empty text is an empty table");
}

void TestParsesHatPlacements() {
    auto hats = ChrHats::Parse("0\t-0.5\t0.25\t1\t1.5\t2\t2.5\n1\t0\t0\t0\t1\t1\t1\n");
    Check(hats != nullptr, "well-formed hat text parses");
    if (hats == nullptr) {
        return;
    }
    const auto* first = hats->At(0);
    Check(first != nullptr && first->xoffset == -0.5f && first->yoffset == 0.25f && first->zoffset == 1.0f &&
              first->xsize == 1.5f && first->ysize == 2.0f && first->zsize == 2.5f,
          "an entry's offsets and sizes are read");
    Check(hats->At(1) != nullptr && hats->At(2) == nullptr, "an index past the table is null");
    Check(ChrHats::Parse("1\t0\t0\t0\t1\t1\t1\n") == nullptr && ChrHats::Parse("0\t0\t0\t0\t1\t1\n") == nullptr &&
              ChrHats::Parse("0\tx\t0\t0\t1\t1\t1\n") == nullptr,
          "entries must count up from 0, have every field, and be numbers");
}

} // namespace

int main() {
    TestParsesHatPlacements();
    TestParsesEverySlot();
    TestRejectsMalformedText();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All chr records tests passed\n");
    return 0;
}
