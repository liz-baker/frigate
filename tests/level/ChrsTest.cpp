#include <cmath>
#include <cstdio>
#include <deque>

#include "LevelFixture.h"
#include "StanFixture.h"
#include "level/BgLevel.h"
#include "level/Chrs.h"
#include "level/Objects.h"
#include "level/PropModels.h"
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

bool Near(float a, float b, float tolerance = 1e-2f) {
    return std::fabs(a - b) < tolerance;
}

constexpr float kFloorSize = 2000.0f;

// The slots the spawns below name: bodies 0 and 1 (BODY_Baron_Samedi is
// list_of_bodies[11]), heads 2 to 4.
const char kRecords[] = "0\tBODY_Jungle_Commando\tCa\t1\t0\n"
                        "1\tBODY_Baron_Samedi\tCb\t1\t0\n"
                        "2\tHEAD_Male_Dave_Dr_Doak\tHa\t1\t1\n"
                        "3\tHEAD_Male_Jim\tHb\t1\t1\n"
                        "4\tHEAD_Female_Sally\tHc\t0\t1\n";

// A guard propdef record (type 9): 7 words.
void PutGuard(std::vector<uint8_t>& file, uint16_t chrnum, uint16_t pad, uint16_t body, uint16_t aiList,
              uint16_t health, uint16_t reaction, uint16_t flags, int16_t head) {
    const size_t at = file.size();
    file.resize(at + 7 * 4, 0);
    PutWords(file, at,
             { 9, static_cast<int32_t>((uint32_t{ chrnum } << 16) | pad),
               static_cast<int32_t>((uint32_t{ body } << 16) | aiList), static_cast<int32_t>(0x0003u << 16 | 0x0004u),
               static_cast<int32_t>((uint32_t{ health } << 16) | reaction),
               static_cast<int32_t>((uint32_t{ flags } << 16) | static_cast<uint16_t>(head)) });
}

// A guard attribute record (type 18): 3 words, grenade probability in the low byte.
void PutAttribute(std::vector<uint8_t>& file, int32_t chrnum, uint8_t grenadeProb) {
    const size_t at = file.size();
    file.resize(at + 3 * 4, 0);
    PutWords(file, at, { 18, chrnum, grenadeProb });
}

// A plain object record (type 3, 32 words) assigned to chr number `chr`.
void PutAssignedObject(std::vector<uint8_t>& file, int16_t chr) {
    const size_t at = file.size();
    file.resize(at + 32 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 3u), static_cast<int32_t>(static_cast<uint16_t>(chr)),
               static_cast<int32_t>(ObjectFlag::AssignedToChr), 0 });
}

// Pads 0 and 1 stand 500 apart and pad 2 stands 10 from pad 0, in world units
// (the file holds them times the level's scale).
std::vector<uint8_t> BuildGuardSetup(float scale) {
    std::vector<uint8_t> file(0x28, 0);
    const size_t padsAt = file.size();
    file.resize(padsAt + 4 * 0x2C, 0);
    const size_t namesAt = file.size();
    file.resize(namesAt + 8, 0);
    std::memcpy(&file[namesAt], "p5a1", 5);
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t propsAt = file.size();

    PutPad(file, padsAt, { 500 * scale, 50 * scale, 700 * scale }, { 1, 0, 0 }, static_cast<uint32_t>(namesAt));
    PutPad(file, padsAt + 0x2C, { 1000 * scale, 50 * scale, 700 * scale }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    PutPad(file, padsAt + 2 * 0x2C, { 510 * scale, 50 * scale, 700 * scale }, { 0, 0, 1 },
           static_cast<uint32_t>(namesAt));
    PutWords(file, 0,
             { 0, 0, static_cast<int32_t>(introAt), static_cast<int32_t>(propsAt), 0, 0, static_cast<int32_t>(padsAt),
               0, 0, 0 });

    // A body chosen from the record with a head drawn from the male heads.
    PutGuard(file, 30, 0, 0, 7, 1500, 60, 0, -1);
    // The stage's random body, a chosen head and sunglasses on a coin flip; a clone.
    PutGuard(file, 31, 1, 0xFFFF, 8, 1000, 90, GuardSetupFlag::RandomSunglasses | GuardSetupFlag::Clone, 3);
    // On the first guard's pad, so its start position is taken.
    PutGuard(file, 32, 2, 0, 9, 1000, 90, 0, -1);
    PutAttribute(file, 31, 40);
    PutAssignedObject(file, 31);
    PutAssignedObject(file, 99);

    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

void TestSpawn() {
    FakeSource source;
    HeaderSpec spec;
    spec.rooms = { { false, false, { 0, 0, 0 } }, { false, false, { 0, 0, 0 } } };
    source.header = BuildHeader(spec);
    source.stan = StanFixture::MakeFloor(static_cast<int16_t>(kFloorSize), 0, 0, 1);
    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "the level loads");
    const float scale = level.Level().scale;

    std::string error;
    auto setup = StageSetup::Parse(BuildGuardSetup(scale), 1.0f / scale, &error);
    Check(setup.has_value(), "the setup parses");
    if (!setup) {
        return;
    }
    const PropDef& first = setup->PropDefs()[0];
    Check(first.Is(PropDefType::Guard) && first.guardChrnum == 30 && first.guardPad == 0 && first.guardBody == 0 &&
              first.guardAiList == 7 && first.guardHealth == 1500 && first.guardReactionTime == 60 &&
              first.guardHead == -1,
          "a guard record's fields are read");
    Check(setup->PropDefs()[1].guardBody == 0xFFFF && setup->PropDefs()[1].guardHead == 3 &&
              setup->PropDefs()[1].guardFlags == (GuardSetupFlag::RandomSunglasses | GuardSetupFlag::Clone) &&
              setup->PropDefs()[3].Is(PropDefType::GuardAttribute) && setup->PropDefs()[3].attributeChrnum == 31 &&
              setup->PropDefs()[3].grenadeProb == 40,
          "so are the other guards' and the attribute record's");

    Stan& stan = level.StanFile();
    setup->ResolveStanTiles(stan);

    Objects objects;
    objects.Reset(level.MaxNumRooms(), &stan, &level,
                  PropModels::Parse("model\t0\tPROP_A\t0.1\t-10\t10\t-20\t20\t-30\t30\n"));
    objects.SetViewerFootprint([](PropCollider*) { return false; });
    stan.SetProps(&objects);
    Chrs chrs;
    chrs.Reset(&stan, &objects.Props(), Frigate::Game::ChrRecords::Parse(kRecords));
    objects.SetChrLookup([&chrs](int chrnum) {
        const Chr* chr = chrs.FindByLiteralId(chrnum);
        return chr != nullptr ? chr->prop : -1;
    });
    objects.SetChrCollision(
        [&chrs](int prop, int cdTypes, PropCollider* out) { return chrs.Collider(prop, cdTypes, out); });
    chrs.AllocateSlots(3);
    Check(chrs.Slots().size() == 13 && chrs.FreeSlots() == 13, "the slots are the guards plus ten");

    // Draws in order: bodiesReset's male head, female head and body, then the
    // first guard's head, then the second's sunglasses.
    std::deque<uint32_t> draws = { 7, 9, 11, 2, 4 };
    const auto random = [&draws]() {
        Check(!draws.empty(), "no more draws are made than the decomp makes");
        const uint32_t value = draws.empty() ? 0 : draws.front();
        if (!draws.empty()) {
            draws.pop_front();
        }
        return value;
    };
    chrs.BodiesReset(random);
    Check(draws.size() == 2, "bodiesReset draws three numbers");

    const ChrTiming timing{ 33, 1.0f, 1 };
    Check(chrs.Spawn(*setup, 0, timing, random), "the first guard spawns");
    const Chr* guard = chrs.ForDef(0);
    Check(guard != nullptr && guard->chrnum == 30 && guard->bodynum == 0, "with the record's number and body");
    // (7 + (2 & 3)) % 25 is male head 9, HEAD_Male_Dave_Dr_Doak.
    Check(guard != nullptr && guard->headnum == 2, "and the head the random draw picks");
    Check(guard != nullptr && Near(guard->hearingScale, 1.5f) && Near(guard->visionRange, 60.0f) &&
              guard->padPreset1 == 3 && guard->chrPreset1 == 4 && guard->aiListId == 7 &&
              (guard->chrflags & (ChrFlag::Clone | ChrFlag::Invincible)) == 0,
          "its record's fields are set");
    Check(guard != nullptr && Near(guard->maxDamage, 4.0f) && guard->aiReturnList == -1 && guard->chrSeeShot == -1 &&
              guard->flinchCount == -1 && guard->lastMoveOk60 == 33 && guard->invalidMove == 0 &&
              (guard->chrflags & ChrFlag::Init) == 0 && (guard->hidden & ChrHidden::Moving) == 0,
          "and the state a fresh chr starts with, once its ground is set");
    if (guard != nullptr) {
        const Prop& prop = objects.Props()[guard->prop];
        Check(prop.type == PropType::Chr && prop.object >= 0 && (prop.flags & PropFlag::Enabled) != 0 &&
                  objects.Props().Active().size() == 1,
              "its prop is an enabled chr prop in the active list");
        Check(Near(guard->ground, 0.0f) && Near(prop.position[1], 50.0f),
              "the chr stands on the floor, its prop at the pad");
        const int rooms[] = { 1, -1 };
        Check(objects.Props().RoomProps(rooms).size() == 1, "the chr is in its room's list");
    }

    // The second guard takes the stage's random body (list_of_bodies[11]), the
    // record's head, and its sunglasses from the draw (4 & 1 == 0).
    Check(chrs.Spawn(*setup, 1, timing, random), "the second guard spawns");
    guard = chrs.ForDef(1);
    Check(guard != nullptr && guard->bodynum == 1 && guard->headnum == 3 && guard->sunglasses &&
              (guard->chrflags & ChrFlag::Clone) != 0 && guard->chrnum == 31,
          "with the random body, its head, sunglasses and the clone flag");
    Check(draws.empty(), "every draw was used");

    // The third guard's pad is inside the first's footprint.
    Check(!chrs.Spawn(*setup, 2, timing, random) && chrs.ForDef(2) == nullptr && chrs.FreeSlots() == 11,
          "a guard whose pad another chr fills does not spawn, and draws nothing");

    chrs.ApplyAttribute(setup->PropDefs()[3]);
    Check(chrs.FindByLiteralId(31) != nullptr && chrs.FindByLiteralId(31)->grenadeProb == 40 &&
              chrs.FindByLiteralId(30)->grenadeProb == 0 && chrs.FindByLiteralId(99) == nullptr,
          "an attribute record sets the named chr's grenade probability");

    // An object assigned to a chr hangs off that chr's prop; one naming no chr is not made.
    Check(objects.Create(*setup, 4), "an object assigned to a chr is created");
    Check(!objects.Create(*setup, 5), "one assigned to a chr that does not exist is not");
    const PlacedObject* held = objects.ForDef(4);
    Check(held != nullptr && objects.Props()[held->prop].parent == chrs.ForDef(1)->prop &&
              objects.Props()[chrs.ForDef(1)->prop].child == held->prop,
          "the object is attached to the chr's prop");

    // chrUpdateCollisionBounds: a diamond of the chr's width about it.
    std::vector<PropCollider> colliders;
    const int rooms[] = { 1, -1 };
    objects.Colliders(rooms, CdType::Chrs, &colliders);
    Check(colliders.size() == 2 && colliders[0].edges == 4 && Near(colliders[0].polygon[0][0], 520.0f) &&
              Near(colliders[0].polygon[1][1], 720.0f) && Near(colliders[0].bottom, 0.0f) &&
              Near(colliders[0].top, 185.0f),
          "a chr's footprint is a diamond from its ground to its height");
    colliders.clear();
    objects.Colliders(rooms, CdType::Objs, &colliders);
    Check(colliders.empty(), "and only chr tests see it");

    // The second chr's steps: across open floor, then into the first.
    const Prop& second = objects.Props()[chrs.ForDef(1)->prop];
    const int slot = second.object;
    const Vec3 from = second.position;
    Vec3 to = { from[0] + 100.0f, from[1], from[2] };
    Check(chrs.TestMove(slot, second.stan, &from, &to, true, timing) != kNoTile && chrs.ForDef(1)->invalidMove == 0 &&
              (chrs.ForDef(1)->hidden & ChrHidden::Moving) == 0,
          "a step across open floor is clear");
    Vec3 target = { 505.0f, from[1], 700.0f };
    const int slid = chrs.TestMove(slot, second.stan, &from, &target, true, timing);
    Check(slid != kNoTile && chrs.ForDef(1)->invalidMove == 2 && !Near(target[2], 700.0f) && target[0] > 505.0f,
          "a step into another chr slides along its footprint's edge");
}

} // namespace

int main() {
    TestSpawn();
    return gFailures == 0 ? 0 : 1;
}
