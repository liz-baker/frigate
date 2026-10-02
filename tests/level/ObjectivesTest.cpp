#include <cstdio>

#include "LevelFixture.h"
#include "level/Inventory.h"
#include "level/Objectives.h"
#include "level/StageSetup.h"

using namespace Frigate::Level;
using namespace LevelFixture;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// An objective start record (type 23): the objective's number and the lowest
// difficulty it applies to.
void PutStart(std::vector<uint8_t>& file, int number, int difficulty) {
    const size_t at = file.size();
    file.resize(at + 16, 0);
    PutWords(file, at, { 23, number, 0, difficulty });
}

// A one-word criterion record naming stage flag bits (types 26 and 27).
void PutFlagCriterion(std::vector<uint8_t>& file, int type, uint32_t bits) {
    const size_t at = file.size();
    file.resize(at + 8, 0);
    PutWords(file, at, { type, static_cast<int32_t>(bits) });
}

void PutEnd(std::vector<uint8_t>& file) {
    const size_t at = file.size();
    file.resize(at + 4, 0);
    PutWords(file, at, { 24 });
}

// A setup with no pads whose propdef section is two objectives: "a" is
// complete once flag 1 is set; "b" (difficulty 1 and up) once flag 4 is,
// and fails when flag 2 is.
std::vector<uint8_t> BuildSetup() {
    std::vector<uint8_t> file(0x28, 0);
    const size_t padsAt = file.size();
    file.resize(padsAt + 0x2C, 0);
    const size_t boundAt = file.size();
    file.resize(boundAt + 0x44, 0);
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t namesAt = file.size();
    file.resize(namesAt + 4, 0);
    const size_t propsAt = file.size();
    PutWords(file, 0,
             { 0, 0, static_cast<int32_t>(introAt), static_cast<int32_t>(propsAt), 0, 0, static_cast<int32_t>(padsAt),
               static_cast<int32_t>(boundAt), 0, 0 });

    PutStart(file, 0, 0);
    PutFlagCriterion(file, 26, 1);
    PutEnd(file);
    PutStart(file, 1, 1);
    PutFlagCriterion(file, 26, 4);
    PutFlagCriterion(file, 27, 2);
    PutEnd(file);

    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

void TestStatus() {
    std::string error;
    const auto setup = StageSetup::Parse(BuildSetup(), 1.0f, &error);
    Check(setup.has_value(), "the setup parses");
    if (!setup) {
        return;
    }

    Objectives objectives;
    objectives.Reset();
    objectives.Build(*setup);
    Check(objectives.Count() == 2, "both objectives are counted");
    Check(objectives.Difficulty(0) == 0 && objectives.Difficulty(1) == 1, "each keeps its difficulty");

    Inventory inventory;
    ObjectiveWorld world;
    world.inventory = &inventory;
    world.difficulty = 1;

    Check(objectives.Status(0, world) == ObjectiveStatus::Incomplete, "an unmet condition leaves it incomplete");
    world.stageFlags = 1;
    Check(objectives.Status(0, world) == ObjectiveStatus::Complete, "a met condition completes it");
    Check(!objectives.AllComplete(world), "one of two is not all");

    world.stageFlags = 1 | 4;
    Check(objectives.AllComplete(world), "both met is all complete");

    world.stageFlags = 1 | 2;
    Check(objectives.Status(1, world) == ObjectiveStatus::Failed, "a fail condition fails it");

    world.stageFlags = 1;
    world.difficulty = 0;
    Check(objectives.AllComplete(world), "an objective of a harder difficulty is left out");
}

void TestStatusChanges() {
    std::string error;
    const auto setup = StageSetup::Parse(BuildSetup(), 1.0f, &error);
    if (!setup) {
        Check(false, "the setup parses");
        return;
    }
    Objectives objectives;
    objectives.Reset();
    objectives.Build(*setup);

    Inventory inventory;
    ObjectiveWorld world;
    world.inventory = &inventory;
    world.difficulty = 1;

    Check(objectives.UpdateStatuses(world).empty(), "nothing changed at the start");
    world.stageFlags = 4;
    const auto changes = objectives.UpdateStatuses(world);
    Check(changes.size() == 1 && changes[0].index == 1 && changes[0].status == ObjectiveStatus::Complete,
          "the second objective completing is reported by its place in the list");
    Check(objectives.UpdateStatuses(world).empty(), "and only once");
}

} // namespace

int main() {
    TestStatus();
    TestStatusChanges();
    return gFailures == 0 ? 0 : 1;
}
