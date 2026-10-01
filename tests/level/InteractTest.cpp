#include <cmath>
#include <cstdio>

#include "LevelFixture.h"
#include "StanFixture.h"
#include "level/BgLevel.h"
#include "level/ClippedDoor.h"
#include "level/Doors.h"
#include "level/Interact.h"
#include "level/Inventory.h"
#include "level/Lang.h"
#include "level/Objects.h"
#include "level/PickupText.h"
#include "level/PlayerWeapons.h"
#include "level/PropLinks.h"
#include "level/PropModels.h"
#include "level/Stan.h"
#include "level/WeaponTable.h"

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
constexpr int kPortalX = 1500;
constexpr float kPi = 3.14159265f;

const char kModels[] = "model\t0\tSOLID\t0.1\t-10\t10\t-20\t20\t-30\t30\t-\t0\n";

void PutRecord(std::vector<uint8_t>& file, uint8_t type, int model, int pad, uint32_t flags, uint32_t flags2,
               int words) {
    const size_t at = file.size();
    file.resize(at + static_cast<size_t>(words) * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | type),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)),
               static_cast<int32_t>(flags), static_cast<int32_t>(flags2) });
}

void PutDoor(std::vector<uint8_t>& file, int pad, uint32_t flags, uint32_t flags2, uint16_t doorType, uint32_t keyFlags,
             uint32_t autoClose) {
    const size_t at = file.size();
    PutRecord(file, 1, 0, pad, flags, flags2, 64);
    PutWords(file, at + 0x80,
             { 0, static_cast<int32_t>(1.0f * 65536.0f), static_cast<int32_t>(0.5f * 65536.0f),
               static_cast<int32_t>(0.1f * 65536.0f), static_cast<int32_t>(0.1f * 65536.0f),
               static_cast<int32_t>(0.2f * 65536.0f) });
    PutU32(file, at + 0x98, doorType);
    PutU32(file, at + 0x9C, keyFlags);
    PutU32(file, at + 0xA0, autoClose);
}

// A record of three words that refers to others.
void PutLinkRecord(std::vector<uint8_t>& file, uint8_t type, int words, std::initializer_list<int32_t> refs) {
    const size_t at = file.size();
    file.resize(at + static_cast<size_t>(words) * 4, 0);
    PutU32(file, at, (0x0100u << 16) | type);
    size_t offset = at + 4;
    for (const int32_t ref : refs) {
        PutU32(file, offset, static_cast<uint32_t>(ref));
        offset += 4;
    }
}

enum Def : size_t {
    kDoorA = 0,
    kKey,
    kDoorB,
    kDoorC,
    kPadlock,
    kLockDoor,
    kTag,
    kRename,
    kSwitch,
    kSafeItemObject,
    kSafe,
    kSafeItem,
};

// Three bound pads on a flat floor (a door on each), one plain pad, and the
// records that join them: door A needs key flag 1, the key that has it, door B
// a two-way swinging door, door C locked with a padlock object, a tag and a
// rename on the key, a switch that opens door B when the padlock is used, and
// a safe item locked behind door A.
std::vector<uint8_t> BuildSetup() {
    std::vector<uint8_t> file(0x28, 0);
    const size_t padsAt = file.size();
    file.resize(padsAt + 2 * 0x2C, 0);
    const size_t boundAt = file.size();
    file.resize(boundAt + 4 * 0x44, 0);
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t namesAt = file.size();
    file.resize(namesAt + 8, 0);
    std::memcpy(&file[namesAt], "p5a1", 5);
    const size_t propsAt = file.size();

    PutPad(file, padsAt, { 500, 0, 500 }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    const Vec3 positions[] = { { kPortalX - 50.0f, 0, 1000 }, { 100, 0, 100 }, { 600, 0, 1400 } };
    const Vec3 looks[] = { { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, 1 } };
    for (size_t i = 0; i < 3; i++) {
        const size_t at = boundAt + i * 0x44;
        PutPad(file, at, positions[i], looks[i], static_cast<uint32_t>(namesAt));
        const float box[] = { -50, 50, 0, 100, -20, 20 };
        for (size_t k = 0; k < 6; k++) {
            PutF32(file, at + 0x2C + k * 4, box[k]);
        }
    }
    PutWords(file, 0,
             { 0, 0, static_cast<int32_t>(introAt), static_cast<int32_t>(propsAt), 0, 0, static_cast<int32_t>(padsAt),
               static_cast<int32_t>(boundAt), 0, 0 });

    PutDoor(file, 0, 0, 0, 0, /*keyFlags=*/1, /*autoClose=*/30);
    PutRecord(file, 4, 0, 0, 0, 0, 33);
    PutU32(file, file.size() - 33 * 4 + 0x80, 1);
    PutDoor(file, 1, ObjectFlag::DoorTwoWay, 0, 5, 0, 0);
    PutDoor(file, 2, 0, 0, 0, 0, 0);
    PutRecord(file, 3, 0, 0, 0, 0, 32);
    PutLinkRecord(file, 38, 4, { -2, -1 });
    PutLinkRecord(file, 22, 4, {});
    PutU32(file, file.size() - 16 + 4, static_cast<uint32_t>(-5) & 0xFFFF);
    PutLinkRecord(file, 37, 10, { -6, 0 });
    PutU32(file, file.size() - 40 + 0x1C, StringId(TextBank::PropObj, 0x05));
    PutLinkRecord(file, 19, 4, { -4, -6 });
    PutRecord(file, 3, 0, 0, 0, 0, 32);
    PutRecord(file, 43, 0, 0, 0, 0, 32);
    PutLinkRecord(file, 44, 5, { -2, -1, -11 });

    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

struct Scene {
    FakeSource source;
    BgLevel level{ source };
    std::optional<StageSetup> setup;
    Objects objects;
    Doors doors;
    PropLinks links;

    bool Build() {
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
        if (!level.Load(LevelId::Dam)) {
            return false;
        }
        std::string error;
        setup = StageSetup::Parse(BuildSetup(), 1.0f / level.Level().scale, &error);
        if (!setup) {
            return false;
        }
        Stan& stan = level.StanFile();
        setup->ResolveStanTiles(stan);
        objects.Reset(level.MaxNumRooms(), &stan, &level, PropModels::Parse(kModels));
        objects.SetViewerFootprint([](PropCollider*) { return false; });
        doors.Reset(&stan, &level, PropModels::Parse(kModels), &objects.Props());
        objects.SetDoorCollision([this](int prop, int cdTypes) { return doors.Passes(prop, cdTypes); },
                                 [this](int prop) { return doors.Collision(prop); });
        stan.SetProps(&objects);
        doors.SetActivatedHook([this](int prop) { links.TriggerSwitches(prop, &doors); });
        doors.SetPadlockCheck([this](size_t def) { return links.IsLocked(def); });
        objects.SetSafeCheck([this](size_t def) { return links.CanPickupFromSafe(def); });
        for (size_t def = 0; def < setup->PropDefs().size(); def++) {
            if (setup->PropDefs()[def].Is(PropDefType::Door)) {
                doors.Create(*setup, def);
            } else {
                objects.Create(*setup, def);
            }
        }
        objects.LinkEmbedded(*setup);
        doors.LinkDoors(*setup);
        links.Build(*setup, &objects, &doors);
        return true;
    }

    int DoorIndex(size_t def) {
        return static_cast<int>(doors.ForDef(def) - doors.All().data());
    }
    InteractScene Interaction() {
        InteractScene scene;
        scene.setup = &*setup;
        scene.stan = &level.StanFile();
        scene.objects = &objects;
        scene.doors = &doors;
        return scene;
    }
};

void TestParse() {
    std::string error;
    auto setup = StageSetup::Parse(BuildSetup(), 1.0f, &error);
    Check(setup.has_value(), "the setup parses");
    if (!setup) {
        return;
    }
    const auto& defs = setup->PropDefs();
    Check(defs.size() == 12 && defs[kLockDoor].refs[0] == -2 && defs[kLockDoor].refs[1] == -1,
          "a lock door record's two targets are read");
    Check(defs[kTag].tagOffset == -5, "a tag's offset is read signed");
    Check(defs[kRename].renameOffset == -6 && defs[kRename].renamePickupText == StringId(TextBank::PropObj, 5),
          "a rename's offset and pickup text are read");
    Check(defs[kSwitch].refs[0] == -4 && defs[kSwitch].refs[1] == -6, "a switch's two targets are read");
    Check(defs[kSafeItem].refs[0] == -2 && defs[kSafeItem].refs[1] == -1 && defs[kSafeItem].refs[2] == -11,
          "a safe item's three targets are read");
}

void TestLinks() {
    Scene scene;
    Check(scene.Build(), "the scene builds");
    if (!scene.setup) {
        return;
    }
    PlacedObject* key = scene.objects.ForDef(kKey);
    Check(key != nullptr && (key->runtimeFlags & ObjectRuntime::Tagged) != 0 &&
              (key->runtimeFlags & ObjectRuntime::Destroyed) != 0,
          "a tag and a rename mark the object they name");

    Inventory inventory;
    PropLinks::AddTextOverrides(*scene.setup, &inventory);
    const TextOverride* text = inventory.TextByObject(static_cast<int64_t>(kKey));
    Check(text != nullptr && text->pickupText == StringId(TextBank::PropObj, 5) && inventory.TextByWeapon(0) == nullptr,
          "a rename adds a text override for its object, not for a weapon");

    const PlacedDoor* doorC = scene.doors.ForDef(kDoorC);
    Check(doorC != nullptr && (doorC->runtimeFlags & ObjectRuntime::PadlockedDoor) != 0 &&
              scene.links.IsLocked(kDoorC) && scene.doors.IsPadlockFree(*doorC) == false,
          "a door with a padlock object is locked");
    PlacedObject* padlock = scene.objects.ForDef(kPadlock);
    Check(padlock != nullptr && (padlock->runtimeFlags & ObjectRuntime::LinkedToSwitch) != 0,
          "the object a switch names is marked as one");
    padlock->state |= ObjectState::Destroyed;
    Check(!scene.links.IsLocked(kDoorC) && scene.doors.IsPadlockFree(*doorC), "and free once the padlock is destroyed");
    padlock->state = 0;

    // A switch: using its object activates the door.
    const PlacedDoor* doorB = scene.doors.ForDef(kDoorB);
    Check(doorB != nullptr && doorB->state == DoorState::Stationary, "door B starts shut");
    scene.links.TriggerSwitches(padlock->prop, &scene.doors);
    Check(doorB->state == DoorState::Opening, "using the switch's object opens the door it names");

    // A safe item is out of reach while its door is shut.
    PlacedDoor* doorA = scene.doors.ForDef(kDoorA);
    const PlacedObject* item = scene.objects.ForDef(kSafeItemObject);
    Check(item != nullptr && (item->flags2 & ObjectFlag2::LinkedToSafe) != 0 &&
              (doorA->flags2 & ObjectFlag2::LinkedToSafe) != 0,
          "a safe item marks its item and its door");
    Check(!scene.links.CanPickupFromSafe(kSafeItemObject) && scene.links.CanPickupFromSafe(kKey),
          "an item in a safe behind a shut door cannot be picked up, any other can");
    doorA->openPosition = 0.6f;
    Check(scene.links.CanPickupFromSafe(kSafeItemObject), "it can once the door is open past half");
    doorA->openPosition = 0.0f;
}

void TestUseDoor() {
    Scene scene;
    if (!scene.Build()) {
        Check(false, "the scene builds");
        return;
    }
    const InteractScene interaction = scene.Interaction();
    Lang lang;
    std::vector<std::string> words(0x41);
    words[0x40] = "Locked.";
    lang.SetBank(TextBank::PropObj, words);

    PlacedDoor* doorA = scene.doors.ForDef(kDoorA);
    InteractPlayer player;
    player.position = doorA->centre;
    Inventory inventory;
    std::string message;
    Interact::UseDoor(interaction, player, doorA->prop, inventory, lang, &message);
    Check(message == "Locked." && doorA->state == DoorState::Stationary &&
              (doorA->flags2 & ObjectFlag2::DoorTriedLocked) != 0 &&
              (doorA->runtimeFlags & ObjectRuntime::Activated) != 0,
          "a door that wants a key the player lacks says so, and stays shut");

    const PlacedObject* key = scene.objects.ForDef(kKey);
    inventory.AddProp(key->prop, static_cast<int64_t>(kKey));
    message.clear();
    Interact::UseDoor(interaction, player, doorA->prop, inventory, lang, &message);
    Check(message.empty() && doorA->state == DoorState::Opening && (doorA->flags2 & ObjectFlag2::DoorTriedLocked) == 0,
          "with the key it opens");
    Check((doorA->runtimeFlags & ObjectRuntime::BeenOpened) != 0, "and is marked opened");

    // A padlocked door stays shut even though it needs no key.
    PlacedDoor* doorC = scene.doors.ForDef(kDoorC);
    message.clear();
    Interact::UseDoor(interaction, player, doorC->prop, inventory, lang, &message);
    Check(doorC->state == DoorState::Stationary && message == "Locked.", "a padlocked door does not open");
    scene.objects.ForDef(kPadlock)->state |= ObjectState::Destroyed;
    Interact::UseDoor(interaction, player, doorC->prop, inventory, lang, &message);
    Check(doorC->state == DoorState::Opening, "until the padlock is destroyed");

    // A two-way door swings away from the side the player is on.
    PlacedDoor* doorB = scene.doors.ForDef(kDoorB);
    doorB->state = DoorState::Stationary;
    doorB->openPosition = 0.0f;
    doorB->flags &= ~ObjectFlag::DoorOpenToFront;
    const Pad& pad = doorB->bound.pad;
    const Vec3 front = { pad.position[0] + 50, pad.position[1], pad.position[2] };
    const Vec3 back = { pad.position[0] - 50, pad.position[1], pad.position[2] };
    scene.doors.ChooseSwingDirection(scene.DoorIndex(kDoorB), back);
    const bool fromBack = (doorB->flags & ObjectFlag::DoorOpenToFront) != 0;
    scene.doors.ChooseSwingDirection(scene.DoorIndex(kDoorB), front);
    const bool fromFront = (doorB->flags & ObjectFlag::DoorOpenToFront) != 0;
    Check(scene.doors.IsInFront(*doorB, front) != scene.doors.IsInFront(*doorB, back) && fromBack != fromFront,
          "a shut two-way door picks its swing by the side of the pad the player is on");
    doorB->openPosition = 10.0f;
    const bool before = (doorB->flags & ObjectFlag::DoorOpenToFront) != 0;
    scene.doors.ChooseSwingDirection(scene.DoorIndex(kDoorB), before ? back : front);
    Check(((doorB->flags & ObjectFlag::DoorOpenToFront) != 0) == before, "and keeps it once it is open");
}

void TestFind() {
    Scene scene;
    if (!scene.Build()) {
        Check(false, "the scene builds");
        return;
    }
    const InteractScene interaction = scene.Interaction();
    PlacedDoor* doorA = scene.doors.ForDef(kDoorA);
    scene.objects.Props()[doorA->prop].flags |= PropFlag::OnScreen;
    const Pad& pad = doorA->bound.pad;

    InteractPlayer player;
    player.position = { pad.position[0] - 100.0f, doorA->centre[1], pad.position[2] };
    player.look = { 1, 0, 0 };
    // Looking along +x: atan2(x, z) of the direction to the pad.
    player.yaw = kPi * 0.5f;
    player.tile = scene.setup->BoundPads()[0].pad.stanTile;
    Check(Interact::Find(interaction, player, { doorA->prop }) == doorA->prop,
          "a door the player is close to and looks at is the one to use");
    player.yaw = -kPi * 0.5f;
    Check(Interact::Find(interaction, player, { doorA->prop }) == -1, "one behind the player is not");
    player.yaw = kPi * 0.5f;
    player.position[0] -= 2000.0f;
    Check(Interact::Find(interaction, player, { doorA->prop }) == -1, "nor one that is far away");
    scene.objects.Props()[doorA->prop].flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
    player.position[0] += 2000.0f;
    Check(Interact::Find(interaction, player, { doorA->prop }) == -1, "nor one that is not on screen");
}

void TestDoorTick() {
    Scene scene;
    if (!scene.Build()) {
        Check(false, "the scene builds");
        return;
    }
    PlacedDoor* door = scene.doors.ForDef(kDoorA);
    const int index = scene.DoorIndex(kDoorA);
    scene.doors.ActivateWrapper(index);
    Check(door->state == DoorState::Opening, "using a shut door opens it");

    int globalTimer = 1;
    for (; globalTimer < 300 && door->state != DoorState::Stationary; globalTimer++) {
        scene.doors.Tick(index, { 1, globalTimer });
    }
    Check(door->state == DoorState::Stationary && Near(door->openPosition, door->maxFrac, 0.05f) &&
              door->openedTime != 0,
          "it finishes opening and notes when");
    const int openedAt = static_cast<int>(door->openedTime);

    for (int tick = 0; tick < 25; tick++) {
        scene.doors.Tick(index, { 1, ++globalTimer });
    }
    Check(door->state == DoorState::Stationary && door->openPosition > 0.9f, "it stays open for its auto-close time");
    for (; globalTimer < openedAt + 60 && door->state == DoorState::Stationary;) {
        scene.doors.Tick(index, { 1, ++globalTimer });
    }
    Check(door->state == DoorState::Closing, "then closes by itself");
    for (int tick = 0; tick < 300 && door->state != DoorState::Stationary; tick++) {
        scene.doors.Tick(index, { 1, ++globalTimer });
    }
    Check(door->state == DoorState::Stationary && Near(door->openPosition, 0.0f) && door->openedTime == 0,
          "and ends shut");

    // A door with something in its way goes back to where it was.
    scene.doors.SetClearCheck([](int) { return false; });
    scene.doors.ActivateWrapper(index);
    scene.doors.Tick(index, { 1, ++globalTimer });
    Check(door->state == DoorState::Opening && Near(door->openPosition, 0.0f) && Near(door->speed, 0.0f),
          "a door blocked while opening stays where it was");
}

void TestClippedVertices() {
    // One quad standing in the xy plane, 0 to 100 along x, 0 to 10 up.
    ModelVertex quad[4]{};
    const int16_t xs[] = { 0, 100, 100, 0 };
    const int16_t ys[] = { 0, 0, 10, 10 };
    const int16_t ss[] = { 0, 1000, 1000, 0 };
    for (size_t i = 0; i < 4; i++) {
        quad[i].position[0] = xs[i];
        quad[i].position[1] = ys[i];
        quad[i].s = ss[i];
        quad[i].t = static_cast<int16_t>(ys[i] * 10);
    }
    // A horizontal slide: everything at or before x = 40 is pulled to it.
    const auto slid = ClipDoorVertices(quad, 4, /*vertical=*/false, 40);
    Check(slid.size() == 4 && slid[0].position[0] == 40 && slid[3].position[0] == 40 && slid[1].position[0] == 100 &&
              slid[2].position[0] == 100,
          "a horizontal cut pulls the vertices before the plane onto it");
    Check(slid[0].s == 400 && slid[3].s == 400 && slid[1].s == 1000,
          "and moves their texture coordinates along the edge");

    const auto risen = ClipDoorVertices(quad, 4, /*vertical=*/true, 4);
    Check(risen[2].position[1] == 4 && risen[3].position[1] == 4 && risen[0].position[1] == 0 && risen[2].t == 40 &&
              risen[3].t == 40,
          "a vertical cut pulls the top vertices down and their coordinates with them");

    const ModelBounds bbox{ 12.4f, 30, -5, 7.6f, 0, 1 };
    Check(DoorClipCutoff(bbox, false) == 12 && DoorClipCutoff(bbox, true) == 8,
          "the cutoff is the box's edge plus a half, truncated");
}

void TestDualWeapons() {
    // Two pistols joined by a link record, the second in the left hand.
    std::vector<uint8_t> file(0x28, 0);
    const size_t padsAt = file.size();
    file.resize(padsAt + 0x2C, 0);
    const size_t boundAt = file.size();
    file.resize(boundAt + 0x44, 0);
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t namesAt = file.size();
    file.resize(namesAt + 8, 0);
    std::memcpy(&file[namesAt], "p5a1", 5);
    const size_t propsAt = file.size();
    PutPad(file, padsAt, { 500, 0, 500 }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    PutWords(file, 0,
             { 0, 0, static_cast<int32_t>(introAt), static_cast<int32_t>(propsAt), 0, 0, static_cast<int32_t>(padsAt),
               static_cast<int32_t>(boundAt), 0, 0 });
    for (const uint32_t flags : { 0u, ObjectFlag::WeaponLeftHanded }) {
        const size_t at = file.size();
        PutRecord(file, 8, 0, 0, flags, 0, 0x22);
        PutU32(file, at + 0x80, (2u << 24) | (0xFFu << 16));
    }
    PutLinkRecord(file, 14, 3, { -2, -1 });
    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });

    FakeSource source;
    HeaderSpec spec;
    spec.rooms = { { false, false, { 0, 0, 0 } } };
    source.header = BuildHeader(spec);
    source.stan = StanFixture::MakeFloor(static_cast<int16_t>(kFloorSize), 0, 0, 1);
    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "the level loads");
    std::string error;
    auto setup = StageSetup::Parse(file, 1.0f / level.Level().scale, &error);
    Check(setup.has_value(), "the dual weapon setup parses");
    if (!setup) {
        return;
    }
    Stan& stan = level.StanFile();
    setup->ResolveStanTiles(stan);
    Objects objects;
    objects.Reset(level.MaxNumRooms(), &stan, &level, PropModels::Parse(kModels));
    objects.SetViewerFootprint([](PropCollider*) { return false; });
    Doors doors;
    doors.Reset(&stan, &level, PropModels::Parse(kModels), &objects.Props());
    objects.Create(*setup, 0);
    objects.Create(*setup, 1);
    PropLinks links;
    links.Build(*setup, &objects, &doors);

    PlacedObject* right = objects.ForDef(0);
    PlacedObject* left = objects.ForDef(1);
    Check(right && left && right->dualWeapon == 1 && left->dualWeapon == 0 && right->weaponLinkedType == 2,
          "a link record pairs two collectables");
    if (!right || !left) {
        return;
    }

    const auto table =
        WeaponTable::Parse("item\t0\tITEM_UNARMED\t0\t0\t0\t0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1."
                           "0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
                           "item\t1\tITEM_FIST\t0\t0\t0\t0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0."
                           "0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
                           "item\t2\tITEM_PISTOL\t1\t1\t7\t0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1."
                           "0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
                           "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n"
                           "ammo\t1\tAMMO_9MM\t20\t0x0\t0.0\n");
    Inventory inventory;
    PlayerWeapons weapons;
    weapons.Init(table, &inventory, [] { return 0u; });
    float armour = 0;
    objects.Collect(*setup, *right, *table, &weapons, &inventory, &armour);
    Check(inventory.HasItem(2) && !inventory.HasDual(2, 2) && right->dualWeapon == -1 && left->dualWeapon == -1 &&
              left->weaponLinkedType == 2,
          "picking up the first adds it alone and unpairs the two");
    objects.Collect(*setup, *left, *table, &weapons, &inventory, &armour);
    Check(inventory.HasDual(2, 2), "and the second completes the set");
}

} // namespace

int main() {
    TestParse();
    TestLinks();
    TestUseDoor();
    TestFind();
    TestDoorTick();
    TestClippedVertices();
    TestDualWeapons();
    return gFailures == 0 ? 0 : 1;
}
