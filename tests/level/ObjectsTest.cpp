#include <algorithm>
#include <cmath>
#include <cstdio>

#include "LevelFixture.h"
#include "StanFixture.h"
#include "level/BgLevel.h"
#include "level/Inventory.h"
#include "level/Objects.h"
#include "level/PlayerWeapons.h"
#include "level/PropList.h"
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

void TestPropModels() {
    const auto table = PropModels::Parse(
        "model\t0\tPROP_A\t0.1\t-1.5\t2\t-3\t4\t-5\t6\t-\nmodel\t1\tPROP_B\t1.0\t-\t-\t-\t-\t-\t-\t-\n");
    Check(table && table->Count() == 2, "the table parses");
    if (!table) {
        return;
    }
    const PropModel* first = table->Model(0);
    Check(first != nullptr && first->name == "PROP_A" && Near(first->scale, 0.1f) && first->bounds &&
              Near(first->bounds->xmin, -1.5f) && Near(first->bounds->zmax, 6.0f),
          "a model's scale and bounds are read");
    Check(!first->weaponSkeleton, "a model without the weapon skeleton is not flagged");
    const auto weapons = PropModels::Parse("model\t0\tchrkalash\t0.1\t-\t-\t-\t-\t-\t-\tprop_weapon\n");
    Check(weapons && weapons->Model(0)->weaponSkeleton, "a model with skeleton_prop_weapon is");
    Check(table->Model(1) != nullptr && !table->Model(1)->bounds && table->Model(2) == nullptr &&
              table->Model(-1) == nullptr,
          "a model with no box has none, and an index outside the table is null");
    Check(!PropModels::Parse("model\t1\tPROP_A\t0.1\t-\t-\t-\t-\t-\t-\t-\n") && !PropModels::Parse("") &&
              !PropModels::Parse("model\t0\tPROP_A\t0.1\t1\t2\t-\n") &&
              !PropModels::Parse("model\t0\tPROP_A\tx\t-\t-\t-\t-\t-\t-\t-\n"),
          "a table out of order, empty or with a bad line is refused");
}

void TestGeometry() {
    const ModelBounds bounds{ -1, 2, -3, 4, -5, 6 };
    Mtxf identity = RotationX(0);
    Check(Near(SumMatrixPos(bounds, identity, 0), -1) && Near(SumMatrixNeg(bounds, identity, 0), 2) &&
              Near(SumMatrixPos(bounds, identity, 1), -3) && Near(SumMatrixNeg(bounds, identity, 1), 4) &&
              Near(SumMatrixPos(bounds, identity, 2), -5) && Near(SumMatrixNeg(bounds, identity, 2), 6),
          "under the identity the sums are the box's low and high bounds");
    // Turned a half turn about y, x and z swap ends.
    const Mtxf turned = RotationY(3.14159265f);
    Check(Near(SumMatrixPos(bounds, turned, 0), -2) && Near(SumMatrixNeg(bounds, turned, 0), 1),
          "a half turn swaps the ends of x");

    Mtxf moved = identity;
    moved.m[3][0] = 10;
    moved.m[3][2] = 20;
    CollisionBlock block;
    BuildCollisionPolygon(bounds, moved, &block);
    Check(block.edges == 4 && Near(block.polygon[0][0], 9) && Near(block.polygon[0][1], 15) &&
              Near(block.polygon[1][0], 12) && Near(block.polygon[1][1], 15) && Near(block.polygon[2][0], 12) &&
              Near(block.polygon[2][1], 26) && Near(block.polygon[3][0], 9) && Near(block.polygon[3][1], 26),
          "an axis-aligned box's footprint is its rectangle, translated");

    // Turned an eighth of a turn about y the footprint is a diamond.
    BuildCollisionPolygon({ -1, 1, -1, 1, -1, 1 }, RotationY(0.78539816f), &block);
    float widest = 0;
    for (int i = 0; i < block.edges; i++) {
        widest = std::max(widest, std::fabs(block.polygon[static_cast<size_t>(i)][0]));
    }
    Check(block.edges >= 4 && Near(widest, std::sqrt(2.0f)), "a box turned 45 degrees has its corners on the axes");

    BuildCollisionPolygon(bounds, moved, &block);
    Check(PointInPolygon({ 10, 0, 20 }, block.polygon, block.edges) &&
              !PointInPolygon({ 5, 0, 20 }, block.polygon, block.edges) &&
              !PointInPolygon({ 10, 0, 40 }, block.polygon, block.edges) &&
              !PointInPolygon({ 10, 0, 20 }, block.polygon, 0),
          "a point is in a footprint or not, and none is in one with no edges");

    BoundPad pad;
    pad.pad.position = { 1, 2, 3 };
    pad.pad.up = { 0, 1, 0 };
    pad.pad.look = { 0, 0, 1 };
    pad.box = { -1, 3, 0, 4, -2, 2 };
    const Vec3 centre = BoundPadCentre(pad);
    Check(Near(centre[0], 2) && Near(centre[1], 4) && Near(centre[2], 3),
          "a bound pad's centre is the middle of its box");
}

void TestPropList() {
    PropList list;
    list.Reset(4, nullptr, nullptr);
    const int first = list.Allocate();
    const int second = list.Allocate();
    Check(first == 0 && second == 1 && list[first].rooms[0] == kNoRoom,
          "props are allocated from the start of the pool");
    list.Free(first);
    Check(list.Allocate() == 0, "a freed prop is the next allocated");

    list.Activate(second);
    list.Activate(0);
    list.Enable(0);
    Check(list.Active() == std::vector<int>({ 1, 0 }) && (list[0].flags & PropFlag::Enabled) != 0 &&
              (list[1].flags & PropFlag::Enabled) == 0,
          "activation appends and enabling sets the flag");
    list.Disable(0);
    Check((list[0].flags & PropFlag::Enabled) == 0, "disabling clears it");

    // A prop in two rooms is found once by a lookup of both.
    list[0].rooms = { 1, 2, kNoRoom };
    list.RegisterRooms(0);
    const int rooms12[] = { 1, 2, -1 };
    const int room2[] = { 2, -1 };
    const int room3[] = { 3, -1 };
    Check(list.RoomProps(rooms12) == std::vector<int16_t>({ 0 }) && list.RoomProps(room2).size() == 1 &&
              list.RoomProps(room3).empty(),
          "a lookup names each prop in its rooms once");

    // A room's props overflow into a second block, and keep their order.
    PropList many;
    many.Reset(2, nullptr, nullptr);
    std::vector<int16_t> expected;
    for (int i = 0; i < 20; i++) {
        const int prop = many.Allocate();
        many.RegisterRoom(prop, 1);
        expected.push_back(static_cast<int16_t>(prop));
    }
    const int room1[] = { 1, -1 };
    Check(many.RoomProps(room1) == expected, "a room holds more props than one block");

    // A removed prop leaves a hole that the next registration takes.
    many.DeregisterRoom(3, 1);
    expected.erase(expected.begin() + 3);
    Check(many.RoomProps(room1) == expected, "deregistering removes the prop");
    const int newcomer = many.Allocate();
    many.RegisterRoom(newcomer, 1);
    expected.insert(expected.begin() + 3, static_cast<int16_t>(newcomer));
    Check(many.RoomProps(room1) == expected, "a new prop takes the freed slot");

    // Emptying the second block frees it for reuse.
    for (int prop = 15; prop < 20; prop++) {
        many.DeregisterRoom(prop, 1);
    }
    const int other = many.Allocate();
    many.RegisterRoom(other, 0);
    const int room0[] = { 0, -1 };
    Check(many.RoomProps(room0) == std::vector<int16_t>({ static_cast<int16_t>(other) }),
          "a room's own props are separate");

    // Attached props: the newest is the parent's child and the rest chain back.
    PropList tree;
    tree.Reset(1, nullptr, nullptr);
    const int host = tree.Allocate();
    const int older = tree.Allocate();
    const int newer = tree.Allocate();
    tree[older].stan = 4;
    tree.Reparent(older, host);
    tree.Reparent(newer, host);
    Check(tree[host].child == newer && tree[newer].prev == older && tree[older].next == newer &&
              tree[older].prev == -1 && tree[newer].next == -1 && tree[older].parent == host &&
              tree[older].stan == kNoTile,
          "attaching props chains them under the parent and clears their tile");
    tree.Detach(older);
    Check(tree[host].child == newer && tree[newer].prev == -1 && tree[older].parent == -1 && tree[older].next == -1,
          "detaching the older one leaves the newer");
    tree.Detach(newer);
    tree.Detach(newer);
    Check(tree[host].child == -1 && tree[newer].parent == -1, "detaching the last, twice, leaves none");
    tree.Activate(host);
    tree.Activate(older);
    tree.Delist(host);
    Check(tree.Active() == std::vector<int>({ older }), "delisting takes a prop out of the active list");

    // Defragmenting fills the holes in a room's first block from its second.
    PropList packed;
    packed.Reset(2, nullptr, nullptr);
    for (int i = 0; i < 17; i++) {
        packed.RegisterRoom(packed.Allocate(), 1);
    }
    packed.DeregisterRoom(3, 1);
    packed.DeregisterRoom(4, 1);
    packed.Defrag();
    std::vector<int16_t> defragmented = { 0, 1, 2, 15, 16 };
    for (int16_t prop = 5; prop < 15; prop++) {
        defragmented.push_back(prop);
    }
    Check(packed.RoomProps(room1) == defragmented, "props from the later block fill the earlier block's holes");
    packed.Defrag();
    Check(packed.RoomProps(room1) == defragmented, "and a packed room is left as it is");

    // The pool runs out.
    PropList pool;
    pool.Reset(1, nullptr, nullptr);
    int last = 0;
    for (int i = 0; i < PropList::kMaxProps; i++) {
        last = pool.Allocate();
    }
    Check(last == PropList::kMaxProps - 1 && pool.Allocate() == -1, "the pool holds kMaxProps props");

    list.DeregisterRooms(0);
    Check(list.RoomProps(rooms12).empty() && list[0].rooms[0] == kNoRoom, "deregistering the rooms clears the list");
    list[0].rooms = { 1, kNoRoom };
    list[0].flags |= PropFlag::KeepRoomList;
    list.RegisterRooms(0);
    list.DeregisterRooms(0);
    Check(list[0].rooms[0] == 1, "a prop flagged to keep its room list does");
}

// A record of the propdef section: the header word, model and pad, flags and
// flags2, then zeros up to `words`.
void PutObject(std::vector<uint8_t>& file, uint8_t type, uint8_t state, int model, int pad, uint32_t flags,
               uint32_t flags2, int words = 32) {
    const size_t at = file.size();
    file.resize(at + static_cast<size_t>(words) * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | (static_cast<uint32_t>(state) << 8) | type),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)),
               static_cast<int32_t>(flags), static_cast<int32_t>(flags2) });
}

constexpr float kFloorSize = 2000.0f;
constexpr int kPortalX = 1500;

// Pads 0 and 1 and bound pad 0 on a flat floor in room 1, and the objects
// the test creates. Positions are in file units.
std::vector<uint8_t> BuildObjectSetup() {
    std::vector<uint8_t> file(0x28, 0);
    // Pads at 0x28, bound pads next, then the end records.
    const size_t padsAt = file.size();
    file.resize(padsAt + 3 * 0x2C, 0);
    const size_t boundAt = file.size();
    file.resize(boundAt + 2 * 0x44, 0);
    const size_t introAt = file.size();
    file.resize(introAt + 4, 0);
    PutWords(file, introAt, { 9 });
    const size_t namesAt = file.size();
    file.resize(namesAt + 8, 0);
    std::memcpy(&file[namesAt], "p5a1", 5);
    const size_t propsAt = file.size();

    PutPad(file, padsAt, { 500, 0, 700 }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    PutPad(file, padsAt + 0x2C, { 1495, 0, 1500 }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    PutPad(file, boundAt, { 800, 0, 1200 }, { 0, 0, 1 }, static_cast<uint32_t>(namesAt));
    const float box[] = { -50, 50, 0, 100, -50, 50 };
    for (size_t i = 0; i < 6; i++) {
        PutF32(file, boundAt + 0x2C + i * 4, box[i]);
    }
    PutWords(file, 0,
             { 0, 0, static_cast<int32_t>(introAt), static_cast<int32_t>(propsAt), 0, 0, static_cast<int32_t>(padsAt),
               static_cast<int32_t>(boundAt), 0, 0 });

    constexpr uint32_t kCollide = ObjectFlag::ForceCollisions;
    // 0: solid, on pad 0.
    PutObject(file, 3, 0, 0, 0, kCollide, 0);
    // 1: solid, on pad 1, by the portal.
    PutObject(file, 3, 0, 0, 1, kCollide, 0);
    // 2: no collision, on pad 0.
    PutObject(file, 5, 0, 1, 0, 0, 0);
    // 3: a door, which is not created here.
    PutObject(file, 1, 0, 0, 0, 0, 0, 64);
    // 4: fitted to the bound pad.
    PutObject(file, 3, 0, 0, 10000, kCollide | ObjectFlag::ScaleToPad, 0);
    // 5: on pad 0, on top of object 0.
    PutObject(file, 3, 0, 0, 0, 0, 0);
    // 6: in the air.
    PutObject(file, 3, 0, 0, 0, ObjectFlag::InAir, 0);
    // 7: assigned to a guard, of which there are none.
    PutObject(file, 3, 0, 0, 3, ObjectFlag::AssignedToChr, 0);
    // 8: inside another object, the one eight records before it.
    PutObject(file, 3, 0, 0, -8, ObjectFlag::InsideAnotherObject, 0);
    // 9: a model with no box.
    PutObject(file, 3, 0, 2, 0, 0, 0);
    // 10: a pad that does not exist.
    PutObject(file, 3, 0, 0, 9, 0, 0);
    // 11: set down at its pad, not walked to it.
    PutObject(file, 3, 0, 0, 0, ObjectFlag::InAir, ObjectFlag2::PlaceAtPad);
    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

const char kModels[] = "model\t0\tPROP_SOLID\t0.1\t-10\t10\t-20\t20\t-30\t30\t-\n"
                       "model\t1\tPROP_SOFT\t0.1\t-10\t10\t-20\t20\t-30\t30\t-\n"
                       "model\t2\tPROP_NOBOX\t0.1\t-\t-\t-\t-\t-\t-\t-\n";

void TestCreateObjects() {
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
    auto setup = StageSetup::Parse(BuildObjectSetup(), 1.0f / scale, &error);
    Check(setup.has_value(), "the setup parses");
    if (!setup) {
        return;
    }
    Stan& stan = level.StanFile();
    setup->ResolveStanTiles(stan);
    Check(setup->Pads()[0].stanTile != kNoTile && setup->Pads()[1].stanTile != kNoTile &&
              setup->BoundPads()[0].pad.stanTile != kNoTile,
          "the pads have tiles");

    Objects objects;
    objects.Reset(level.MaxNumRooms(), &stan, &level, PropModels::Parse(kModels));
    objects.SetViewerFootprint([](PropCollider*) { return false; });
    stan.SetProps(&objects);
    std::vector<bool> created;
    for (size_t def = 0; def < setup->PropDefs().size(); def++) {
        created.push_back(objects.Create(*setup, def));
    }
    objects.LinkEmbedded(*setup);
    const std::vector<bool> expectCreated = {
        true, true, true, false, true, true, true, false, true, false, false, true
    };
    Check(created == expectCreated, "each record is created or not as its type, pad and model allow");
    const auto& all = objects.All();
    Check(all.size() == 8 && objects.Props().Active().size() == 7, "an object inside another is not activated");
    if (all.size() != 8) {
        return;
    }
    // The creation order of the created ones: 0, 1, 2, 4, 5, 6, 8, 11.
    const PlacedObject& solid = all[0];
    const PlacedObject& byPortal = all[1];
    const PlacedObject& soft = all[2];
    const PlacedObject& fitted = all[3];
    const PlacedObject& stacked = all[4];
    const PlacedObject& hovering = all[5];
    const PlacedObject& embedded = all[6];
    const PlacedObject& placed = all[7];

    // The models are 0.1 scale, so the solid box is 1 by 2 by 3 world units
    // either side of its centre, which sits its height off the floor plus 4.
    Check(solid.collision && solid.collision->edges == 4 && Near(solid.runtimePosition[1], 6.0f) &&
              Near(solid.collision->bottom, 4.0f) && Near(solid.collision->top, 8.0f) &&
              (solid.state & ObjectState::ExtCollisionBlock) != 0,
          "a solid object rests on the floor with its footprint's height range");
    const float padX = 500.0f / scale;
    const float padZ = 700.0f / scale;
    Check(Near(solid.runtimePosition[0], padX) && Near(solid.runtimePosition[2], padZ) &&
              Near(solid.collision->polygon[0][0], padX - 1.0f) && Near(solid.collision->polygon[0][1], padZ - 3.0f),
          "and its footprint is its box around its pad");
    Check(!soft.collision && objects.Collision(soft.prop) == nullptr && objects.Collision(solid.prop) != nullptr,
          "an object without the collision flag has no footprint");

    // The stacked object comes to rest on the solid one.
    Check((stacked.runtimeFlags & ObjectRuntime::RestingOnObject) != 0 && Near(stacked.runtimePosition[1], 10.0f) &&
              (solid.runtimeFlags & ObjectRuntime::RestingOnObject) == 0,
          "an object placed over another rests on top of it");
    Check(Near(hovering.runtimePosition[1], 2.0f) && Near(placed.runtimePosition[1], 2.0f) &&
              Near(placed.runtimePosition[0], padX),
          "an object in the air hangs from its pad by its box");

    // The fitted object takes the smallest ratio on every axis, then scales
    // its model by the largest.
    const float fit = 100.0f / scale;
    const float ratioX = fit / (20.0f * 0.1f);
    const float ratioZ = fit / (60.0f * 0.1f);
    Check(Near(fitted.scale, 0.1f * ratioX, 0.05f) && Near(fitted.mtx.m[0][0], fitted.scale * ratioZ / ratioX, 0.05f),
          "an object scaled to a bound pad fits its box");
    Check(embedded.prop >= 0 && objects.Props()[embedded.prop].rooms[0] == kNoRoom &&
              objects.Props()[embedded.prop].type == PropType::Obj && objects.Props()[embedded.prop].stan == kNoTile,
          "an object inside another has a prop but no place");
    Check(objects.Props()[embedded.prop].parent == solid.prop && objects.Props()[solid.prop].child == embedded.prop &&
              (embedded.runtimeFlags & ObjectRuntime::HasOwner) != 0 && objects.ForDef(0) == &solid &&
              objects.ForDef(3) == nullptr,
          "and is attached to the record its pad number names");

    // The rooms each object is in: the one by the portal reaches room 2.
    const int room1[] = { 1, -1 };
    const int room2[] = { 2, -1 };
    const auto& inRoom1 = objects.Props().RoomProps(room1);
    Check(std::count(inRoom1.begin(), inRoom1.end(), solid.prop) == 1 &&
              std::count(inRoom1.begin(), inRoom1.end(), byPortal.prop) == 1,
          "objects are found in their room");
    const auto& inRoom2 = objects.Props().RoomProps(room2);
    Check(inRoom2.size() == 1 && inRoom2[0] == byPortal.prop,
          "and one reaching a portal's box is found in the next room");

    // The stan sees the footprints.
    int tile = setup->Pads()[0].stanTile;
    const float x = padX;
    const float z = padZ;
    Check(stan.TestLineUnobstructed(&tile, x - 10, z, x + 10, z, CdType::Objs, 100, 0, 100, 0) == 0 &&
              stan.LastCollision().prop == solid.prop && Near(stan.LastCollision().fraction, 0.45f, 0.01f),
          "a line through a solid object is stopped by it");
    tile = setup->Pads()[0].stanTile;
    Check(stan.TestLineUnobstructed(&tile, x - 10, z, x + 10, z, 0, 100, 0, 100, 0) != 0,
          "and passes with no collision types");
    tile = setup->Pads()[0].stanTile;
    Check(stan.TestVolume(&tile, x - 2, z, 1.5f, CdType::Objs, 100, 0) == 2 && stan.LastCollision().prop == solid.prop,
          "a circle at its edge is a hit");
    tile = setup->Pads()[0].stanTile;
    Check(stan.TestVolume(&tile, x - 2, z, 1.5f, CdType::Objs, 5.0f, 4.5f) == 2, "in its height range too");
    tile = setup->Pads()[0].stanTile;
    Check(stan.TestVolume(&tile, x - 2, z, 1.5f, CdType::Doors, 100, 0) < 0, "and a door-only test ignores it");

    // The portal search sees the window.
    Check(level.FindWindowPortal({ static_cast<float>(kPortalX) / scale - 10, 0, 1000.0f / scale },
                                 { static_cast<float>(kPortalX) / scale + 10, 0, 1000.0f / scale }) == 0 &&
              level.FindWindowPortal({ 100, 0, 100 }, { 110, 0, 100 }) == -1,
          "the window portal is the one a segment crosses");
}

// A collectable propdef record (type 8, 0x22 words): weaponnum and
// LinkedWeaponType packed at offset 0x80, as StageSetup::Parse reads them.
void PutCollectable(std::vector<uint8_t>& file, int model, int pad, uint32_t flags, int8_t weaponNum,
                    int8_t linkedType) {
    const size_t at = file.size();
    file.resize(at + 0x22 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 8u),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)),
               static_cast<int32_t>(flags), 0 });
    PutU32(file, at + 0x80,
           (static_cast<uint32_t>(static_cast<uint8_t>(weaponNum)) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(linkedType)) << 16));
}

// A multi-ammo crate propdef record (type 20, 0x2D words): 13 {model,
// quantity} slots at offset 0x80.
void PutAmmoCrate(std::vector<uint8_t>& file, int model, int pad,
                  const std::array<uint16_t, PropDef::kAmmoSlotCount>& quantities) {
    const size_t at = file.size();
    file.resize(at + 0x2D * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 20u),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)), 0,
               0 });
    for (size_t i = 0; i < PropDef::kAmmoSlotCount; i++) {
        PutU32(file, at + 0x80 + (i * 4), quantities[i]);
    }
}

// A body armour propdef record (type 21, 0x22 words): initialamount at
// offset 0x80, fixed-point.
void PutArmourRecord(std::vector<uint8_t>& file, int model, int pad, float amount) {
    const size_t at = file.size();
    file.resize(at + 0x22 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 21u),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)), 0,
               0 });
    PutWords(file, at + 0x80, { static_cast<int32_t>(amount * 65536.0f) });
}

// A key propdef record (type 4, 33 words): keyflags at offset 0x80.
void PutKeyRecord(std::vector<uint8_t>& file, int model, int pad, uint32_t keyFlags) {
    const size_t at = file.size();
    file.resize(at + 33 * 4, 0);
    PutWords(file, at,
             { static_cast<int32_t>((0x0100u << 16) | 4u),
               static_cast<int32_t>((static_cast<uint32_t>(model) << 16) | (static_cast<uint32_t>(pad) & 0xFFFF)), 0,
               0 });
    PutU32(file, at + 0x80, keyFlags);
}

const char kPickupModels[] = "model\t0\tPICKUP\t0.1\t-1\t1\t-1\t1\t-1\t1\t-\n";

// Item 2 is a pistol taking AMMO_9MM (type 1), item 3 an unlisted weapon with
// no ammo type.
const char kPickupWeapons[] =
    "item\t0\tITEM_UNARMED\t0\t0\t0\t0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\n"
    "item\t1\tITEM_FIST\t0\t0\t0\t0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\n"
    "item\t2\tITEM_PISTOL\t1\t1\t7\t0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\n"
    "ammo\t0\tAMMO_NONE\t0\n"
    "ammo\t1\tAMMO_9MM\t20\n";

// One pad on a flat floor, and one record of each pickup type #159 places:
// an armed collectable (record 0), an unarmed one that placement skips
// (record 1), a key (2), an ammo crate granting 9mm (3) and body armour (4).
std::vector<uint8_t> BuildPickupSetup() {
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

    PutCollectable(file, 0, 0, 0, /*weaponNum=*/2, /*linkedType=*/-1);
    PutCollectable(file, 0, 0, 0, /*weaponNum=*/kItemUnarmed, /*linkedType=*/-1);
    PutKeyRecord(file, 0, 0, 0x1);
    std::array<uint16_t, PropDef::kAmmoSlotCount> quantities{};
    quantities[0] = 5; // slot 0 -> AMMO_9MM
    PutAmmoCrate(file, 0, 0, quantities);
    PutArmourRecord(file, 0, 0, 50.0f);

    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

// One pad on a flat floor, and one record of each #160 type: a CCTV (0), an
// autogun (1), a hanging-monitor rack (2), a monitor on its own pad (3), a
// monitor with no pad and no owner to embed in (4, which Create defers --
// https://github.com/liz-baker/frigate/issues/176), a monitor embedded in
// the rack (5, three records back), a multi-monitor (6), a vehicle (7), an
// aircraft with forced collisions (8), a tank (9), a plain object with
// forced collisions on the same pad and model as a control for the
// aircraft's collision-height adjustment (10), and a monitor flagged to
// render in the post-background pass (11).
std::vector<uint8_t> BuildSpecialObjectSetup() {
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

    auto words = [](PropDefType type) { return static_cast<int>(PropDefWords(static_cast<uint8_t>(type))); };
    PutObject(file, static_cast<uint8_t>(PropDefType::Cctv), 0, 0, 0, 0, 0, words(PropDefType::Cctv));
    PutObject(file, static_cast<uint8_t>(PropDefType::Autogun), 0, 0, 0, 0, 0, words(PropDefType::Autogun));
    PutObject(file, static_cast<uint8_t>(PropDefType::Rack), 0, 0, 0, 0, 0, words(PropDefType::Rack));
    PutObject(file, static_cast<uint8_t>(PropDefType::Monitor), 0, 0, 0, 0, 0, words(PropDefType::Monitor));
    // Sits on the second switch of the rack, two records back.
    const size_t sittingAt = file.size();
    PutObject(file, static_cast<uint8_t>(PropDefType::Monitor), 0, 0, -1, 0, 0, words(PropDefType::Monitor));
    PutWords(file, sittingAt + 0xF4, { -2, 1 });
    // Embedded three records back, in the rack (record 2).
    PutObject(file, static_cast<uint8_t>(PropDefType::Monitor), 0, 0, -3, ObjectFlag::InsideAnotherObject, 0,
              words(PropDefType::Monitor));
    PutObject(file, static_cast<uint8_t>(PropDefType::MultiMonitor), 0, 0, 0, 0, 0, words(PropDefType::MultiMonitor));
    PutObject(file, static_cast<uint8_t>(PropDefType::Vehicle), 0, 0, 0, 0, 0, words(PropDefType::Vehicle));
    PutObject(file, static_cast<uint8_t>(PropDefType::Aircraft), 0, 0, 0, ObjectFlag::ForceCollisions, 0,
              words(PropDefType::Aircraft));
    PutObject(file, static_cast<uint8_t>(PropDefType::Tank), 0, 0, 0, 0, 0, words(PropDefType::Tank));
    PutObject(file, static_cast<uint8_t>(PropDefType::Prop), 0, 0, 0, ObjectFlag::ForceCollisions, 0);
    PutObject(file, static_cast<uint8_t>(PropDefType::Monitor), 0, 0, 0, ObjectFlag::MonitorRenderPostBg, 0,
              words(PropDefType::Monitor));

    const size_t end = file.size();
    file.resize(end + 4, 0);
    PutWords(file, end, { 0x30 });
    return file;
}

void TestSpecialObjects() {
    FakeSource source;
    HeaderSpec spec;
    spec.rooms = { { false, false, { 0, 0, 0 } } };
    source.header = BuildHeader(spec);
    source.stan = StanFixture::MakeFloor(static_cast<int16_t>(kFloorSize), 0, 0, 1);
    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "the level loads for the special-object setup");
    const float scale = level.Level().scale;

    std::string error;
    auto setup = StageSetup::Parse(BuildSpecialObjectSetup(), 1.0f / scale, &error);
    Check(setup.has_value(), "the special-object setup parses");
    if (!setup) {
        return;
    }
    Stan& stan = level.StanFile();
    setup->ResolveStanTiles(stan);

    Objects objects;
    objects.Reset(level.MaxNumRooms(), &stan, &level, PropModels::Parse(kPickupModels));
    objects.SetViewerFootprint([](PropCollider*) { return false; });
    std::vector<bool> created;
    for (size_t def = 0; def < setup->PropDefs().size(); def++) {
        created.push_back(objects.Create(*setup, def));
    }
    objects.LinkEmbedded(*setup);
    const std::vector<bool> expectCreated = { true, true, true, true, true, true, true, true, true, true, true, true };
    Check(created == expectCreated, "a CCTV, autogun, rack, monitor, multi-monitor, vehicle, aircraft and tank are all "
                                    "placed like a plain object, and a monitor with no pad sits on its owner");

    const PlacedObject* cctv = objects.ForDef(0);
    const PlacedObject* autogun = objects.ForDef(1);
    const PlacedObject* rack = objects.ForDef(2);
    const PlacedObject* monitor = objects.ForDef(3);
    const PlacedObject* embeddedMonitor = objects.ForDef(5);
    const PlacedObject* multiMonitor = objects.ForDef(6);
    const PlacedObject* vehicle = objects.ForDef(7);
    const PlacedObject* aircraft = objects.ForDef(8);
    const PlacedObject* tank = objects.ForDef(9);
    const PlacedObject* control = objects.ForDef(10);
    const PlacedObject* postBgMonitor = objects.ForDef(11);
    Check(cctv && autogun && rack && monitor && embeddedMonitor && multiMonitor && vehicle && aircraft && tank &&
              control && postBgMonitor,
          "every placed special object is found by its def index");
    if (!cctv || !autogun || !rack || !monitor || !embeddedMonitor || !multiMonitor || !vehicle || !aircraft || !tank ||
        !control || !postBgMonitor) {
        return;
    }
    const PlacedObject* sitting = objects.ForDef(4);
    Check(sitting != nullptr && (sitting->runtimeFlags & ObjectRuntime::Embedded) != 0 &&
              objects.Props()[sitting->prop].parent == rack->prop && sitting->embedSlot == 1 &&
              objects.Props()[sitting->prop].stan == kNoTile &&
              (objects.Props()[sitting->prop].flags & PropFlag::Enabled) == 0,
          "a monitor with no pad is embedded in its owner's second switch, unplaced and not enabled");
    // RotationX(20 degrees), scaled by the monitor's scale over its owner's (1).
    Check(sitting != nullptr && Near(sitting->embedment.m[1][1], std::cos(0.36651915f)) &&
              Near(sitting->embedment.m[1][2], std::sin(0.36651915f)) && Near(sitting->embedment.m[3][0], 0.0f),
          "leaning back by its embedment matrix");
    Check(objects.ForProp(sitting->prop) == sitting && objects.ForProp(-1) == nullptr, "and its prop finds it");

    Check((objects.Props()[monitor->prop].flags & PropFlag::RenderPostBg) == 0 &&
              (objects.Props()[postBgMonitor->prop].flags & PropFlag::RenderPostBg) != 0,
          "a monitor's render-post-bg flag carries over to its prop only when set");

    Check(objects.Props()[embeddedMonitor->prop].parent == rack->prop &&
              (embeddedMonitor->runtimeFlags & ObjectRuntime::HasOwner) != 0 &&
              objects.Props()[embeddedMonitor->prop].stan == kNoTile,
          "the embedded monitor attaches to the rack the same way an embedded plain object does");

    // An aircraft's collision footprint sits kAircraftDrop (200 units) lower
    // than an identically-placed, identically-modeled object's, matching
    // chrobjCollisionRelated's own adjustment for it.
    const CollisionBlock* aircraftCollision = objects.Collision(aircraft->prop);
    const CollisionBlock* controlCollision = objects.Collision(control->prop);
    Check(aircraftCollision != nullptr && controlCollision != nullptr,
          "the aircraft and its plain-object control both have a forced collision block");
    if (aircraftCollision != nullptr && controlCollision != nullptr) {
        Check(Near(aircraftCollision->bottom, controlCollision->bottom - 200.0f, 0.5f),
              "the aircraft's collision bottom is dropped 200 units below where it rests");
    }
}

void TestPickups() {
    FakeSource source;
    HeaderSpec spec;
    spec.rooms = { { false, false, { 0, 0, 0 } } };
    source.header = BuildHeader(spec);
    source.stan = StanFixture::MakeFloor(static_cast<int16_t>(kFloorSize), 0, 0, 1);
    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "the level loads");
    const float scale = level.Level().scale;

    std::string error;
    auto setup = StageSetup::Parse(BuildPickupSetup(), 1.0f / scale, &error);
    Check(setup.has_value(), "the pickup setup parses");
    if (!setup) {
        return;
    }
    Check(Near(setup->PropDefs()[4].armourAmount, 50.0f) && setup->PropDefs()[0].weaponNum == 2 &&
              setup->PropDefs()[2].keyFlags == 0x1 && setup->PropDefs()[3].ammoSlots[0].quantity == 5,
          "a pickup record's type-specific fields are read");

    Stan& stan = level.StanFile();
    setup->ResolveStanTiles(stan);

    Objects objects;
    objects.Reset(level.MaxNumRooms(), &stan, &level, PropModels::Parse(kPickupModels));
    objects.SetViewerFootprint([](PropCollider*) { return false; });
    std::vector<bool> created;
    for (size_t def = 0; def < setup->PropDefs().size(); def++) {
        created.push_back(objects.Create(*setup, def));
    }
    const std::vector<bool> expectCreated = { true, false, true, true, true };
    Check(created == expectCreated, "every pickup places except the unarmed collectable");

    const PlacedObject* weapon = objects.ForDef(0);
    const PlacedObject* key = objects.ForDef(2);
    const PlacedObject* ammo = objects.ForDef(3);
    const PlacedObject* armour = objects.ForDef(4);
    Check(weapon && key && ammo && armour, "the placed pickups are found by their def index");
    if (!weapon || !key || !ammo || !armour) {
        return;
    }

    const auto table = WeaponTable::Parse(kPickupWeapons);
    Check(table != nullptr, "the pickup weapon table parses");
    const Vec3 playerPos = weapon->runtimePosition;
    const int playerStan = objects.Props()[weapon->prop].stan;
    const Vec3 farAway = { playerPos[0] + 10000.0f, playerPos[1], playerPos[2] };
    float playerArmour = 0.0f;

    // The weapon: out of range fails, in range and unheld passes, and
    // collecting it gives the item plus its starting ammo. Held but not yet
    // full (max ammo 20, held 10) stays pickable; topped up to full it stops
    // being one, since it has no linked weapon to still pair with.
    {
        Inventory inventory;
        PlayerWeapons weapons;
        weapons.Init(table, &inventory, [] { return 0u; });
        Check(!objects.TestPickup(*setup, *weapon, farAway, playerStan, playerArmour, inventory, weapons, *table),
              "a pickup out of range fails the proximity test");
        Check(objects.TestPickup(*setup, *weapon, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "and one in range and unheld passes it");
        Check(objects.Collect(*setup, *weapon, *table, &weapons, &inventory, &playerArmour) == PickupOutcome::Collected,
              "collecting a weapon frees the prop");
        Check(inventory.HasItem(2) && weapons.AmmoHeld(1) == 10,
              "and gives the player the weapon and its starting ammo");
        Check(objects.TestPickup(*setup, *weapon, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "held but not full, it is still pickable (for its ammo)");
        weapons.GiveAmmo(1, 20);
        Check(!objects.TestPickup(*setup, *weapon, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "held and fully ammoed with no linked weapon, it no longer is");
    }

    // The ammo crate: adds its slot quantities to the ammo already held,
    // gated on there being room, independent of the weapon scenario above.
    {
        Inventory inventory;
        PlayerWeapons weapons;
        weapons.Init(table, &inventory, [] { return 0u; });
        weapons.GiveAmmo(1, 10);
        Check(objects.TestPickup(*setup, *ammo, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "a crate with room in at least one of its ammo types is pickable");
        Check(objects.Collect(*setup, *ammo, *table, &weapons, &inventory, &playerArmour) == PickupOutcome::Collected,
              "collecting an ammo crate frees the prop");
        Check(weapons.AmmoHeld(1) == 15, "and adds its slots' quantities to the ammo already held");
        weapons.GiveAmmo(1, 20);
        Check(!objects.TestPickup(*setup, *ammo, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "a crate whose only nonzero ammo type is full is no longer pickable");
    }

    // Armour: pickable while it beats what the player holds, and collecting
    // it sets (not adds to) the player's armour.
    {
        Inventory inventory;
        PlayerWeapons weapons;
        weapons.Init(table, &inventory, [] { return 0u; });
        Check(objects.TestPickup(*setup, *armour, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "armour worth more than the player's current amount is pickable");
        Check(objects.Collect(*setup, *armour, *table, &weapons, &inventory, &playerArmour) == PickupOutcome::Collected,
              "collecting armour frees the prop");
        Check(Near(playerArmour, 50.0f), "and sets (not adds to) the player's armour");
        Check(!objects.TestPickup(*setup, *armour, playerPos, playerStan, playerArmour, inventory, weapons, *table),
              "armour no better than what the player already has is no longer pickable");
    }

    // The key: propPickupByPlayer keeps it as a live, prop-backed inventory
    // entry rather than freeing it -- not modelled yet (#182).
    {
        Inventory inventory;
        PlayerWeapons weapons;
        weapons.Init(table, &inventory, [] { return 0u; });
        Check(objects.Collect(*setup, *key, *table, &weapons, &inventory, &playerArmour) == PickupOutcome::None,
              "collecting a key leaves the prop in place");
    }
}

} // namespace

int main() {
    TestPropModels();
    TestGeometry();
    TestPropList();
    TestCreateObjects();
    TestSpecialObjects();
    TestPickups();
    return gFailures == 0 ? 0 : 1;
}
