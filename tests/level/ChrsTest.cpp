#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>

#include "LevelFixture.h"
#include "StanFixture.h"
#include "level/BgLevel.h"
#include "level/Chrs.h"
#include "level/ModelHandle.h"
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

// A model that records what the level code asks of it.
class FakeModel final : public ModelHandle {
  public:
    explicit FakeModel(std::string path) : path(std::move(path)) {
        playback.hasRoot = true;
    }
    Frigate::ModelPlayback& Playback() override {
        return playback;
    }
    bool AttachTo(ModelHandle& holder, size_t slot, float roll) override {
        heldBy = &holder;
        heldSlot = slot;
        heldRoll = roll;
        return true;
    }
    void SetSwitchVisible(size_t slot, bool visible) override {
        switches.emplace_back(slot, visible);
    }
    void SetGunfireVisible(size_t slot, bool visible) override {
        gunfire.emplace_back(slot, visible);
    }

    std::string path;
    Frigate::ModelPlayback playback;
    ModelHandle* heldBy = nullptr;
    size_t heldSlot = 0;
    float heldRoll = 0.0f;
    std::vector<std::pair<size_t, bool>> switches;
    std::vector<std::pair<size_t, bool>> gunfire;
    std::string headPath;
    bool sunglasses = false;
};

class FakeFactory final : public ModelFactory {
  public:
    std::shared_ptr<ModelHandle> CreateBody(const std::string& bodyPath, const std::string& headPath,
                                            bool sunglasses) override {
        auto model = std::make_shared<FakeModel>(bodyPath);
        model->headPath = headPath;
        model->sunglasses = sunglasses;
        made.push_back(model);
        return model;
    }
    std::shared_ptr<ModelHandle> CreateProp(const std::string& modelPath) override {
        auto model = std::make_shared<FakeModel>(modelPath);
        made.push_back(model);
        return model;
    }
    std::vector<std::shared_ptr<FakeModel>> made;
};

bool Near(float a, float b, float tolerance = 1e-2f) {
    return std::fabs(a - b) < tolerance;
}

constexpr float kFloorSize = 2000.0f;

// The slots the spawns below name: bodies 0 and 1 (BODY_Baron_Samedi is
// list_of_bodies[11]), heads 2 to 4.
const char kRecords[] = "0\tBODY_Jungle_Commando\tCa\t1\t0\t2.0\t0.5\t100.0\n"
                        "1\tBODY_Baron_Samedi\tCb\t1\t0\t1.0\t1.0\t100.0\n"
                        "2\tHEAD_Male_Dave_Dr_Doak\tHa\t1\t1\t1.0\t1.0\t100.0\n"
                        "3\tHEAD_Male_Jim\tHb\t1\t1\t1.0\t1.0\t100.0\n"
                        "4\tHEAD_Female_Sally\tHc\t0\t1\t1.0\t1.0\t100.0\n";

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

// A collectable (weapon) record, type 8, 34 words, held by chr number `chr`.
void PutHeldWeapon(std::vector<uint8_t>& file, int16_t chr, uint16_t model, uint32_t flags, int8_t weaponNum) {
    const size_t at = file.size();
    file.resize(at + 34 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 8u),
               static_cast<int32_t>((uint32_t{ model } << 16) | static_cast<uint16_t>(chr)),
               static_cast<int32_t>(flags | ObjectFlag::AssignedToChr), 0 });
    PutWords(file, at + 0x80,
             { static_cast<int32_t>((uint32_t{ static_cast<uint8_t>(weaponNum) } << 24) | 0x00FF0000u) });
}

// A hat record, type 17, 32 words, worn by chr number `chr`.
void PutHeldHat(std::vector<uint8_t>& file, int16_t chr, uint16_t model) {
    const size_t at = file.size();
    file.resize(at + 32 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0200u << 16) | 17u),
               static_cast<int32_t>((uint32_t{ model } << 16) | static_cast<uint16_t>(chr)),
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
    // A gun in the second guard's right hand, one in its left (paired), a hat,
    // and a gun for a chr that does not exist.
    PutHeldWeapon(file, 31, 1, 0, 5);
    PutHeldWeapon(file, 31, 1, ObjectFlag::WeaponLeftHanded | ObjectFlag::WeaponIsDouble, 5);
    PutHeldHat(file, 31, 2);
    PutHeldWeapon(file, 99, 1, 0, 5);

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
                  PropModels::Parse("model\t0\tPROP_A\t0.1\t-10\t10\t-20\t20\t-30\t30\t-\t0\n"
                                    "model\t1\tgun\t0.2\t-1\t1\t-1\t1\t-1\t1\tprop_weapon\t0\n"
                                    "model\t2\thatperk\t0.1\t-1\t1\t-1\t1\t-1\t1\t-\t0\n"));
    FakeFactory factory;
    objects.SetModels(&factory);
    objects.SetViewerFootprint([](PropCollider*) { return false; });
    stan.SetProps(&objects);
    Chrs chrs;
    chrs.Reset(&stan, &objects.Props(), Frigate::Game::ChrRecords::Parse(kRecords), nullptr, &factory);
    objects.SetChrLookup([&chrs](int chrnum) {
        ChrHost host;
        if (Chr* chr = chrs.FindByLiteralId(chrnum)) {
            host.prop = chr->prop;
            host.model = chr->model;
            host.weaponsHeld = &chr->weaponsHeld;
            host.hat = &chr->hat;
        }
        return host;
    });
    objects.SetChrCollision(
        [&chrs](int prop, int cdTypes, PropCollider* out) { return chrs.Collider(prop, cdTypes, out); });
    // The global list 7, which the first guard's record names.
    AiLists lists;
    Check(lists.ParseGlobals("7\t04\n"), "the AI lists parse");
    chrs.SetAiLists(&lists);
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
    // makeonebody: the body's record scales it, the head's model goes on it.
    auto* body = guard != nullptr ? dynamic_cast<FakeModel*>(guard->model.get()) : nullptr;
    Check(body != nullptr && body->path == "models/chr/Ca" && body->headPath == "models/chr/Ha" && !body->sunglasses,
          "the character has the body's model with the head on it");
    Check(body != nullptr && Near(body->playback.scale, 2.0f * 0.10000001f, 1e-5f) &&
              Near(body->playback.animTranslationScale, 0.5f, 1e-5f),
          "scaled by the body's record, and its animation translation by the pov");
    Check(guard != nullptr && Near(guard->boundingRadius, 100.0f), "with the model's bounding radius");
    // init_GUARDdata_with_set_values: setsuboffset, setsubroty, modelSetAnimPlaySpeed.
    Check(body != nullptr && Near(body->playback.root.pos.x, 500.0f) && Near(body->playback.root.pos.z, 700.0f) &&
              Near(body->playback.root.heading, std::atan2(1.0f, 0.0f)) && Near(body->playback.playSpeed, 1.0f) &&
              static_cast<bool>(body->playback.moveFilter),
          "the model is placed at the pad, facing its look, with the root-move callback set");
    if (body != nullptr) {
        // The callback is sub_GAME_7F01FC10: a step settles on the floor.
        chrs.SetTiming({ 33, 1.0f, 1 });
        body->playback.root.pos.y = 300.0f;
        body->playback.CalcPos();
        Check(Near(body->playback.root.pos.x, 500.0f) && Near(body->playback.root.pos.y, 0.0f),
              "and a root move stays on the floor");
    }
    Check(guard != nullptr && Near(guard->hearingScale, 1.5f) && Near(guard->visionRange, 60.0f) &&
              guard->padPreset1 == 3 && guard->chrPreset1 == 4 && guard->aiList != nullptr && guard->aiList->id == 7 &&
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
    auto* secondModel = guard != nullptr ? dynamic_cast<FakeModel*>(guard->model.get()) : nullptr;
    Check(secondModel != nullptr && secondModel->sunglasses && secondModel->headPath == "models/chr/Hb",
          "whose model has the head and shows its sunglasses");
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

    // weaponAssignToHome and setupHat: the guns and the hat the chr holds.
    Chr* holder = chrs.FindByLiteralId(31);
    Check(objects.Create(*setup, 6), "a weapon assigned to a chr is created");
    const PlacedObject* right = objects.ForDef(6);
    Check(right != nullptr && objects.Props()[right->prop].type == PropType::Weapon &&
              holder->weaponsHeld[HandRight] == right->prop && objects.Props()[right->prop].parent == holder->prop,
          "as a weapon prop the chr holds in its right hand");
    auto* rightModel = right != nullptr ? dynamic_cast<FakeModel*>(right->instance.get()) : nullptr;
    Check(rightModel != nullptr && rightModel->path == "models/prop/PgunZ" && rightModel->heldBy == secondModel &&
              rightModel->heldSlot == 3 && Near(rightModel->heldRoll, 0.0f) &&
              Near(rightModel->playback.scale, 0.2f * (256.0f / 256.0f)),
          "its model hangs on the hand's node at the model's scale");
    Check(rightModel != nullptr && rightModel->gunfire.size() == 1 &&
              rightModel->gunfire[0] == std::pair<size_t, bool>(0, false) && rightModel->switches.size() == 1 &&
              rightModel->switches[0] == std::pair<size_t, bool>(2, false),
          "with the muzzle flash and the hidden switch off");
    Check(right != nullptr && right->weaponNum == 5 && right->weaponLinkedType == -1, "and the record's weapon");

    Check(objects.Create(*setup, 7), "a left-handed weapon is created");
    const PlacedObject* left = objects.ForDef(7);
    auto* leftModel = left != nullptr ? dynamic_cast<FakeModel*>(left->instance.get()) : nullptr;
    Check(left != nullptr && holder->weaponsHeld[HandLeft] == left->prop && leftModel != nullptr &&
              leftModel->heldSlot == 5 && Near(leftModel->heldRoll, 3.14159265f),
          "held in the left hand, turned half a turn");
    right = objects.ForDef(6);
    Check(left != nullptr && right != nullptr && left->weaponLinkedType == 5 && right->weaponLinkedType == 5,
          "and paired with the right hand's weapon");

    Check(objects.Create(*setup, 8), "a hat assigned to a chr is created");
    const PlacedObject* hat = objects.ForDef(8);
    auto* hatModel = hat != nullptr ? dynamic_cast<FakeModel*>(hat->instance.get()) : nullptr;
    Check(hat != nullptr && holder->hat == hat->prop && objects.Props()[hat->prop].type == PropType::Obj &&
              objects.Props()[hat->prop].parent == holder->prop && hatModel != nullptr &&
              hatModel->path == "models/prop/PhatperkZ" && hatModel->heldBy == secondModel && hatModel->heldSlot == 6 &&
              Near(hatModel->playback.scale, 0.1f * 2.0f),
          "a hat is worn on the head's node, at its extra scale");
    Check(!objects.Create(*setup, 9), "a weapon assigned to a chr that does not exist is not created");
    Check(objects.Props().Active().size() == 2, "and none of the held things is in the active list");

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
