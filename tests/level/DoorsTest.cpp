#include <cmath>
#include <cstdio>

#include "LevelFixture.h"
#include "StanFixture.h"
#include "level/BgLevel.h"
#include "level/Doors.h"
#include "level/PropModels.h"
#include "level/Stan.h"
#include "level/StanProps.h"

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

bool Near(float a, float b, float tolerance = 1e-2f) {
    return std::fabs(a - b) < tolerance;
}

// A door propdef record (type 1), 64 words. `maxFrac`/`perimFrac`/`accel`/
// `decel`/`maxSpeed` are already the fixed-point-converted values
// StageSetup::Parse reads back out.
void PutDoor(std::vector<uint8_t>& file, int model, int pad, uint32_t flags, uint32_t flags2, int32_t linkedOffset,
             float maxFrac, float perimFrac, float accel, float decel, float maxSpeed, uint16_t doorFlags,
             uint16_t doorType, uint32_t keyFlags) {
    const size_t at = file.size();
    file.resize(at + 64 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 1u),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)),
               static_cast<int32_t>(flags), static_cast<int32_t>(flags2) });
    PutWords(file, at + 0x80,
             { linkedOffset, static_cast<int32_t>(maxFrac * 65536.0f), static_cast<int32_t>(perimFrac * 65536.0f),
               static_cast<int32_t>(accel * 65536.0f), static_cast<int32_t>(decel * 65536.0f),
               static_cast<int32_t>(maxSpeed * 65536.0f) });
    PutU32(file, at + 0x98, (static_cast<uint32_t>(doorFlags) << 16) | doorType);
    PutU32(file, at + 0x9C, keyFlags);
}

constexpr float kFloorSize = 2000.0f;
constexpr int kPortalX = 1500;

const char kModels[] = "model\t0\tDOOR_SOLID\t0.1\t-10\t10\t-20\t20\t-30\t30\t-\n";

// One bound pad straddling the portal in the +z-facing wall, and two door
// records: a sliding door on it (record 0) and, linked to it, a stationary
// closed one elsewhere (record 1) that only Activate's sibling propagation
// should move.
std::vector<uint8_t> BuildDoorSetup(uint32_t doorFlags, uint16_t motionDoorFlags = 0, float perimFrac = 0.5f) {
    std::vector<uint8_t> file(0x28, 0);
    const size_t padsAt = file.size();
    file.resize(padsAt + 0x2C, 0); // terminator only, no plain pads
    const size_t boundAt = file.size();
    file.resize(boundAt + 3 * 0x44, 0); // two bound pads plus a terminator
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t namesAt = file.size();
    file.resize(namesAt + 8, 0);
    std::memcpy(&file[namesAt], "p5a1", 5);
    const size_t propsAt = file.size();

    PutPad(file, boundAt, { static_cast<float>(kPortalX) - 50.0f, 0, 1000 }, { 1, 0, 0 },
           static_cast<uint32_t>(namesAt));
    const float box0[] = { -50, 50, 0, 100, -20, 20 };
    for (size_t i = 0; i < 6; i++) {
        PutF32(file, boundAt + 0x2C + i * 4, box0[i]);
    }
    PutPad(file, boundAt + 0x44, { 100, 0, 100 }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    const float box1[] = { -50, 50, 0, 100, -20, 20 };
    for (size_t i = 0; i < 6; i++) {
        PutF32(file, boundAt + 0x44 + 0x2C + i * 4, box1[i]);
    }
    PutWords(file, 0,
             { 0, 0, static_cast<int32_t>(introAt), static_cast<int32_t>(propsAt), 0, 0, static_cast<int32_t>(padsAt),
               static_cast<int32_t>(boundAt), 0, 0 });

    // Record 0: the door under test, opening at a fixed rate, linked one
    // record ahead to record 1.
    PutDoor(file, 0, 0, doorFlags, 0, /*linkedOffset=*/1, /*maxFrac=*/1.0f, perimFrac, /*accel=*/0.1f,
            /*decel=*/0.1f, /*maxSpeed=*/0.2f, motionDoorFlags, /*doorType=*/0, /*keyFlags=*/0);
    // Record 1: the linked sibling, on the other bound pad.
    PutDoor(file, 0, 1, 0, 0, /*linkedOffset=*/0, 1.0f, perimFrac, 0.1f, 0.1f, 0.2f, 0, 0, 0);

    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

void TestCreateAndStateMachine() {
    FakeSource source;
    HeaderSpec spec;
    spec.rooms = { { false, false, { 0, 0, 0 } }, { false, false, { 0, 0, 0 } } };
    spec.portals = { PortalSpec{
        1,
        2,
        0,
        0,
        { { kPortalX, -100, 0 }, { kPortalX, -100, 2000 }, { kPortalX, 100, 2000 }, { kPortalX, 100, 0 } } } };
    source.header = BuildHeader(spec);
    source.stan = StanFixture::MakeFloor(static_cast<int16_t>(kFloorSize), 0, 0, 1);
    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "the level loads");
    const float scale = level.Level().scale;

    std::string error;
    auto setup = StageSetup::Parse(BuildDoorSetup(0), 1.0f / scale, &error);
    Check(setup.has_value(), "the setup parses");
    if (!setup) {
        return;
    }
    Check(Near(setup->PropDefs()[0].doorMaxFrac, 1.0f) && Near(setup->PropDefs()[0].doorPerimFrac, 0.5f) &&
              Near(setup->PropDefs()[0].doorAccel, 0.1f) && setup->PropDefs()[0].doorLinkedOffset == 1,
          "a door record's fixed-point fields convert to float");

    Stan& stan = level.StanFile();
    setup->ResolveStanTiles(stan);
    Check(setup->BoundPads()[0].pad.stanTile != kNoTile && setup->BoundPads()[1].pad.stanTile != kNoTile,
          "the bound pads have tiles");

    PropList props;
    props.Reset(level.MaxNumRooms(), &stan, &level);
    Doors doors;
    doors.Reset(&stan, &level, PropModels::Parse(kModels), &props);
    Check(doors.Create(*setup, 0), "the first door is created");
    Check(doors.Create(*setup, 1), "the linked door is created");
    doors.LinkDoors(*setup);

    const PlacedDoor* door = doors.ForDef(0);
    const PlacedDoor* sibling = doors.ForDef(1);
    Check(door != nullptr && sibling != nullptr, "both doors are placed");
    if (door == nullptr || sibling == nullptr) {
        return;
    }
    Check(door->linkedDoor >= 0 && &doors.All()[static_cast<size_t>(door->linkedDoor)] == sibling,
          "a door's linkedDoorOffset resolves to the next record");
    Check(door->state == DoorState::Stationary && Near(door->openPosition, 0.0f) && door->collision &&
              door->collision->edges > 0,
          "a closed door starts stationary with a footprint");
    Check(doors.Passes(door->prop, CdType::ClosedDoors) && !doors.Passes(door->prop, CdType::OpenDoors),
          "propDoorGetCdTypes reports a closed door as closed, not open");

    int globalTimer = 0;
    // Opening: the sibling (linked, not activated directly) tracks it.
    doors.Activate(0, DoorState::Opening);
    Check(door->state == DoorState::Opening && sibling->state == DoorState::Opening,
          "activating a door also opens its linked sibling");
    for (int i = 0; i < 200 && door->state != DoorState::Stationary; i++) {
        doors.Tick(0, { 1, ++globalTimer });
    }
    Check(door->state == DoorState::Stationary && Near(door->openPosition, door->maxFrac, 0.05f),
          "ticking drives the door to fully open, then stops it");
    Check(doors.Passes(door->prop, CdType::OpenDoors) && !doors.Passes(door->prop, CdType::ClosedDoors),
          "a fully open door reports CdType::OpenDoors");
    Check(door->collision->edges == 0, "an open door (perimFrac <= openPosition) has no footprint");

    // Closing back down.
    doors.Activate(0, DoorState::Closing);
    for (int i = 0; i < 200 && door->state != DoorState::Stationary; i++) {
        doors.Tick(0, { 1, ++globalTimer });
    }
    Check(door->state == DoorState::Stationary && Near(door->openPosition, 0.0f, 0.05f) && door->collision->edges > 0,
          "closing brings the door back to shut with its footprint restored");
}

} // namespace

int main() {
    TestCreateAndStateMachine();
    return gFailures == 0 ? 0 : 1;
}
