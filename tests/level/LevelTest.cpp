#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "level/AttractDemo.h"
#include "level/BgHeader.h"
#include "level/BgLevel.h"
#include "level/BgView.h"
#include "level/DemoPlayback.h"
#include "level/BgVisibility.h"
#include "level/IntroSection.h"
#include "level/Inventory.h"
#include "level/Lang.h"
#include "level/PickupText.h"
#include "level/WeaponTable.h"
#include "level/LevelInfo.h"
#include "level/StageSetup.h"
#include "LevelFixture.h"
#include "StanFixture.h"

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

void TestLevelTable() {
    Check(FindLevelIndex(LevelId::Bunker1) == 0, "the first stage is row 0");
    Check(FindLevelIndex(LevelId::Control) == 3, "Control is row 3");
    Check(FindLevelIndex(LevelId::Jungle) == 17, "Jungle is row 17");
    Check(FindLevelIndex(LevelId::Title) == 0, "an id with no row gets row 0");
    Check(FindLevelIndex(LevelId::Stack) == FindLevelIndex(LevelId::Stack) &&
              std::strcmp(LevelEntryAt(FindLevelIndex(LevelId::Stack)).bgName, "bg_ame") == 0,
          "Stack shares Basement's rooms");
    const LevelEntry& dam = LevelEntryAt(FindLevelIndex(LevelId::Dam));
    Check(std::strcmp(dam.bgName, "bg_dam") == 0 && dam.visibility == 0.2f, "Dam's row");
}

void TestHeaderParse() {
    HeaderSpec spec;
    spec.rooms = { { true, false, { 10, 20, 30 } }, { false, true, { 1, 2, 3 } } };
    spec.portals = { WallPortal(1, 2, 5.0f), WallPortal(2, 1, 7.0f) };
    spec.visibility = { { 5, 0 }, { 100, 1 }, { 6, 9 } };
    auto header = ParseBgHeader(BuildHeader(spec));
    Check(header.has_value(), "a well-formed header parses");
    if (!header) {
        return;
    }
    Check(header->maxNumRooms == 3, "two rooms plus the end entry count as three");
    Check(header->rooms.size() == 4, "rooms include the placeholder");
    Check(header->rooms[1].vertexBlob != 0 && header->rooms[1].secondaryList == 0, "room 1: vertices, no secondary");
    Check(header->rooms[2].vertexBlob == 0 && header->rooms[2].secondaryList != 0, "room 2: secondary, no vertices");
    Check(header->rooms[1].position == Vec3{ 10, 20, 30 }, "room position");
    Check(header->portals.size() == 2 && header->portals[0].points.size() == 4, "portals and their points");
    Check(header->portals[1].room1 == 2 && header->portals[1].room2 == 1, "portal rooms");
    Check(header->visibilityOffset != 0, "visibility offset");

    // The ALT record's argument becomes the portal's index, big-endian.
    size_t alt = header->visibilityOffset + 8;
    uint32_t argument = (uint32_t{ header->bytes[alt + 4] } << 24) | (uint32_t{ header->bytes[alt + 5] } << 16) |
                        (uint32_t{ header->bytes[alt + 6] } << 8) | header->bytes[alt + 7];
    Check(argument == 1, "ALT visibility record names portal 1");
    Check(header->bytes[header->visibilityOffset + 4 + 3] == 0, "other visibility records are untouched");
}

void TestHeaderRejects() {
    Check(!ParseBgHeader(std::vector<uint8_t>(8, 0)).has_value(), "a header shorter than its words is rejected");

    HeaderSpec spec;
    spec.rooms = { { true, false, {} } };
    auto bytes = BuildHeader(spec);
    PutU32(bytes, 4, Addr(0x100000));
    Check(!ParseBgHeader(bytes).has_value(), "a room table past the region is rejected");

    bytes = BuildHeader(spec);
    PutU32(bytes, 4, 0x05000014);
    Check(!ParseBgHeader(bytes).has_value(), "a room table in another segment is rejected");

    HeaderSpec noRoomsEnd;
    noRoomsEnd.rooms = { { true, false, {} } };
    bytes = BuildHeader(noRoomsEnd);
    // Fill the null terminator so the table never ends.
    PutU32(bytes, 0x14 + 3 * 0x18 + 4, Addr(0x1234));
    Check(!ParseBgHeader(bytes).has_value(), "a room table that runs off the region is rejected");

    HeaderSpec other;
    other.rooms = { { true, false, {} } };
    other.firstWord = 1;
    auto parsed = ParseBgHeader(BuildHeader(other));
    Check(parsed.has_value() && parsed->rooms.empty() && parsed->firstWord == 1,
          "a file whose first word is not 0 parses to no rooms");
}

void TestLoadComputesBounds() {
    HeaderSpec spec;
    spec.rooms = { { true, false, { 100, 0, 0 } }, { true, false, { 0, 0, 0 } }, { false, false, {} } };
    // Room 1 spans x 1..11 (+100), room 2 x 0..4; room 3 has no vertices.
    FakeSource source;
    source.header = BuildHeader(spec);
    source.geometries[1] = Vertices({ { 1, -2, 3 }, { 11, 5, -7 } });
    source.geometries[2] = Vertices({ { 0, 0, 0 }, { 4, 1, 1 } });

    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "Load succeeds");
    Check(level.MaxNumRooms() == 4, "three rooms plus the end entry");
    Check(level.Room(1).minBounds == Vec3{ 101, -2, -7 } && level.Room(1).maxBounds == Vec3{ 111, 5, 3 },
          "bounds are the vertices' extent plus the room position");
    Check(level.Room(2).maxBounds[0] == 4, "second room's bounds");
    Check(level.Room(3).minBounds == Vec3{ 0, 0, 0 }, "a room with no vertices keeps zero bounds");
    // Bounds are worked out by loading each room and dropping it again.
    Check(source.loads[1] == 1 && source.loads[2] == 1 && source.loads.count(3) == 0,
          "each room with vertices is loaded once for its bounds; the rest are not");
    Check(level.Room(1).modelBinLoaded == 0 && level.Room(2).modelBinLoaded == 0, "no room stays resident");
    Check(source.residentWatch[1].expired(), "an unloaded room's resources are released");
    Check(level.RoomLoadBudget() == 200, "the room budget starts at 200");
    Check(level.VisibilityScale() == 0.2f, "Dam's visibility scale");
}

void TestRoomBoundsFromStan() {
    HeaderSpec spec;
    spec.rooms = { { true, false, { 100, 0, 0 } }, { false, false, { 7, 7, 7 } }, { false, false, {} } };
    FakeSource source;
    source.header = BuildHeader(spec);
    source.geometries[1] = Vertices({ { 1, -2, 3 }, { 11, 5, -7 } });
    // Room 2 has tiles; room 3 has none.
    source.stan = StanFixture::MakeFloor(100, -5, 10, 2);

    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "a stage with a stan loads");
    Check(level.StanFile().Loaded() && level.StanFile().Scale() == level.Level().scale,
          "the stan takes the stage's scale");
    Check(level.Room(2).minBounds == Vec3{ 0, -5, 0 } && level.Room(2).maxBounds == Vec3{ 100, 5, 100 },
          "a room with no vertices takes the stan's room bounds");
    Check(level.RoomEntry(2).position == Vec3{ 50, 0, 50 }, "and their centre as its position");
    Check(level.Room(3).minBounds == Vec3{ 0, 0, 0 } && level.RoomEntry(3).position == Vec3{ 0, 0, 0 },
          "a room the stan does not cover is left as it was");
    Check(level.Room(1).minBounds == Vec3{ 101, -2, -7 }, "a room with vertices ignores the stan");

    source.stan = nullptr;
    Check(!level.Load(LevelId::Dam) && level.Error().find("stan") != std::string::npos,
          "a stage with no stan file fails");
}

void TestLoadFailures() {
    FakeSource source;
    BgLevel level(source);
    Check(!level.Load(LevelId::Dam) && !level.Error().empty(), "a missing header fails with a reason");

    HeaderSpec spec;
    spec.rooms = { { true, false, {} } };
    spec.portals = { PortalSpec{ 1, 1, 0, 0, { { 0, 0, 0 }, { 1, 0, 0 } } } };
    source.header = BuildHeader(spec);
    Check(!level.Load(LevelId::Dam) && level.Error().find("portal 0") != std::string::npos,
          "a portal with fewer than three points fails");

    HeaderSpec other;
    other.rooms = { { true, false, {} } };
    other.firstWord = 7;
    source.header = BuildHeader(other);
    Check(level.Load(LevelId::Dam) && level.MaxNumRooms() == 0 && level.RoomLoadBudget() == 200,
          "a file with a nonzero first word loads with no rooms");
}

void TestRoomLifecycle() {
    HeaderSpec spec;
    spec.rooms = { { true, false, {} }, { true, false, {} }, { true, false, {} } };
    FakeSource source;
    source.header = BuildHeader(spec);
    BgLevel level(source);
    level.Load(LevelId::Dam);
    source.loads.clear();

    Check(level.CheckIfRoomModelNeedsLoad(1), "an unloaded room needs loading");
    Check(level.Room(1).modelBinLoaded == 1 && level.Room(1).requested, "and is then resident and requested");
    Check(!level.CheckIfRoomModelNeedsLoad(1), "a resident room needs nothing");
    Check(source.loads[1] == 1, "and is not loaded twice");
    Check(!level.CheckIfRoomModelNeedsLoad(-1) && !level.CheckIfRoomModelNeedsLoad(500),
          "out-of-range rooms are ignored");

    level.LoadRoomModelData(4);
    Check(source.loads.count(4) == 0, "the end entry is not a room");

    // A room nobody has asked for ages one step a tick and unloads on the
    // fourth; a requested one stays.
    level.LoadRoomModelData(2);
    for (int tick = 1; tick <= 3; tick++) {
        level.TickUnload();
        Check(level.Room(2).modelBinLoaded == 1 + tick, "an unrequested room ages");
    }
    level.TickUnload();
    Check(level.Room(2).modelBinLoaded == 0 && source.residentWatch[2].expired(), "and unloads once it reaches 4");
    Check(level.Room(1).modelBinLoaded == 1, "a requested room never ages");

    source.failRoom = 3;
    level.LoadRoomModelData(3);
    Check(level.Room(3).modelBinLoaded == 0 && !level.Error().empty(), "a room that fails to load stays unloaded");

    level.LoadRoomModelData(2);
    level.UnloadRooms();
    Check(level.Room(1).modelBinLoaded == 0 && level.Room(2).modelBinLoaded == 0, "UnloadRooms drops every room");

    level.LoadRoomModelData(1);
    level.Cleanup();
    Check(level.Room(1).modelBinLoaded == 0 && level.VisibilityScale() == 1.0f, "Cleanup unloads and resets the scale");
}

void TestPortalPreparation() {
    // Room 1 spans x -10..0 and room 2 x 10..20, and the portal between
    // them sits at x = 5 with its normal towards +x.
    HeaderSpec spec;
    spec.rooms = { { true, false, { -5, 0, 0 } }, { true, false, { 15, 0, 0 } } };
    PortalSpec portal = WallPortal(1, 2, 5.0f);
    if (NormalX(portal) < 0) {
        std::swap(portal.points[1], portal.points[3]);
    }
    spec.portals = { portal, portal };
    // The second portal names the rooms the wrong way round.
    std::swap(spec.portals[1].room1, spec.portals[1].room2);
    spec.portals[1].control1 = 0x03;

    FakeSource source;
    source.header = BuildHeader(spec);
    source.geometries[1] = Vertices({ { -5, 0, 0 }, { 5, 0, 0 } });
    source.geometries[2] = Vertices({ { -5, 0, 0 }, { 5, 0, 0 } });
    BgLevel level(source);
    Check(level.Load(LevelId::Dam), "portal level loads");

    Check(level.PortalNormalIsHorizontal(0), "a wall portal's normal is horizontal");
    Check(level.Portals()[0].room1 == 1 && level.Portals()[0].room2 == 2, "a correctly ordered portal keeps its rooms");
    Check(level.Portals()[1].room1 == 1 && level.Portals()[1].room2 == 2, "a misordered portal is swapped");
    Check((level.Portals()[0].controlBytes1 & 0x01) == 0 && (level.Portals()[1].controlBytes1 & 0x01) == 0,
          "bit 0 of control byte 1 is cleared");
    Check((level.Portals()[1].controlBytes1 & 0x02) != 0, "other bits are kept");

    // Portal points widen their rooms' bounds.
    Check(level.Room(1).maxBounds[0] == 5 && level.Room(2).minBounds[0] == 5,
          "portal points extend the bounds of both rooms");
}

void TestSpecialPortals() {
    // Jungle (row 17) marks portals 0..0x3A.
    HeaderSpec spec;
    spec.rooms = { { true, false, {} } };
    for (int i = 0; i < 0x3C; i++) {
        spec.portals.push_back(WallPortal(1, 1, 0.0f));
    }
    FakeSource source;
    source.header = BuildHeader(spec);
    BgLevel level(source);
    Check(level.Load(LevelId::Jungle), "Jungle loads");
    Check((level.Portals()[0].controlBytes1 & 0x02) != 0 && (level.Portals()[0x3A].controlBytes1 & 0x02) != 0,
          "the marked range is flagged");
    Check((level.Portals()[0x3B].controlBytes1 & 0x02) == 0, "the portal after it is not");

    BgLevel other(source);
    other.Load(LevelId::Dam);
    Check((other.Portals()[0].controlBytes1 & 0x02) == 0, "other stages are left alone");
}

// Every real bg file's header, when a decomp checkout is at hand.
void TestRealFiles() {
    const char* dir = std::getenv("FRIGATE_DECOMP_BG_DIR");
    if (dir == nullptr) {
        return;
    }
    int parsed = 0;
    for (const auto& file : std::filesystem::directory_iterator(dir)) {
        if (file.path().extension() != ".bin" || file.path().filename().string().rfind("bg_", 0) != 0 ||
            file.path().stem().string().find("_all_p") == std::string::npos) {
            continue;
        }
        std::ifstream in(file.path(), std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.size() < 0x30) {
            continue;
        }
        uint32_t roomTable =
            (uint32_t{ data[4] } << 24 | uint32_t{ data[5] } << 16 | uint32_t{ data[6] } << 8 | data[7]) & 0xFFFFFF;
        if (roomTable == 0) {
            continue;
        }
        size_t first = roomTable + 0x18;
        uint32_t vertices = (uint32_t{ data[first] } << 24 | uint32_t{ data[first + 1] } << 16 |
                             uint32_t{ data[first + 2] } << 8 | data[first + 3]) &
                            0xFFFFFF;
        if (vertices == 0) {
            continue;
        }
        size_t size = ((vertices - 1) | 0xF) + 1;
        auto header = ParseBgHeader(std::vector<uint8_t>(data.begin(), data.begin() + size));
        std::string name = file.path().filename().string();
        Check(header.has_value(), (name + " header parses").c_str());
        if (!header) {
            continue;
        }
        bool planar = true;
        for (const BgPortal& portal : header->portals) {
            planar = planar && portal.points.size() >= 3;
        }
        Check(planar, (name + ": every portal has at least three points").c_str());
        Check(header->maxNumRooms > 1, (name + " has rooms").c_str());
        parsed++;
    }
    Check(parsed > 0, "real bg files were found");
}

// A square portal of half-size 5 in the plane z = `z`, spanning x0..x1.
PortalSpec ZPortal(uint8_t room1, uint8_t room2, float z, float x0, float x1) {
    return { room1, room2, 0, 0, { { x0, -5, z }, { x1, -5, z }, { x1, 5, z }, { x0, 5, z } } };
}

// Sets the length byte of the visibility record at index `record`.
void SetRecordLength(std::vector<uint8_t>& bytes, size_t record, uint8_t length) {
    size_t base = (size_t(bytes[13]) << 16) | (size_t(bytes[14]) << 8) | bytes[15];
    bytes[base + record * 8 + 1] = length;
}

// A camera at the origin looking down -z on a 100x100 screen; world and
// camera space agree once the stage scale is cancelled.
VisibilityView FrontView(int currentRoom) {
    VisibilityView view;
    float scale = LevelEntryAt(FindLevelIndex(LevelId::Dam)).scale;
    for (int i = 0; i < 3; i++) {
        view.worldToScreen[i][i] = scale;
    }
    view.worldToScreen[3][3] = 1;
    view.screen = { 0, 0, 100, 100 };
    view.zFar = 1e6f;
    view.halfWidth = 50;
    view.halfHeight = 50;
    view.recipScaleX = 50;
    view.recipScaleY = 50;
    view.currentRoom = currentRoom;
    return view;
}

// Rooms 1 and 2 straight ahead, room 3 either ahead (x 0) or far to the side.
struct Corridor {
    Corridor(float room3X, std::vector<std::pair<uint8_t, int>> commands = {}) : level(source), visibility(level) {
        HeaderSpec spec;
        spec.rooms = { { true, false, {} }, { true, false, {} }, { true, false, {} } };
        spec.portals = { ZPortal(1, 2, -25, -5, 5), ZPortal(2, 3, -45, room3X - 5, room3X + 5) };
        spec.visibility = std::move(commands);
        source.header = BuildHeader(spec);
        source.geometries[1] = Vertices({ { -10, -10, -20 }, { 10, 10, -10 } });
        source.geometries[2] = Vertices({ { -10, -10, -40 }, { 10, 10, -30 } });
        source.geometries[3] = Vertices({ { int16_t(room3X - 10), -10, -60 }, { int16_t(room3X + 10), 10, -50 } });
    }

    FakeSource source;
    BgLevel level;
    BgVisibility visibility;
};

std::vector<int> DrawnRooms(const BgVisibility& visibility) {
    std::vector<int> rooms;
    for (const RoomDraw& draw : visibility.RoomsDrawn()) {
        rooms.push_back(draw.room);
    }
    return rooms;
}

void TestRects() {
    ScreenRect a{ 0, 0, 10, 10 };
    Check(RectIntersect(a, { 5, 5, 20, 20 }) && a.left == 5 && a.top == 5 && a.right == 10 && a.bottom == 10,
          "an overlap shrinks to the shared area");

    ScreenRect edge{ 0, 0, 10, 10 };
    Check(!RectIntersect(edge, { 10, 0, 20, 10 }) && edge.left == edge.right,
          "rectangles that only touch do not overlap");

    ScreenRect u{ 0, 0, 4, 4 };
    RectUnion(u, { 2, -1, 9, 3 });
    Check(u.left == 0 && u.top == -1 && u.right == 9 && u.bottom == 4, "a union covers both");

    Check(RectIsInside({ 2, 2, 3, 3 }, { 0, 0, 10, 10 }) && !RectIsInside({ 20, 2, 30, 3 }, { 0, 0, 10, 10 }),
          "inside is tested on the top-left corner");
}

void TestVisibleThroughPortals() {
    Corridor stage(0);
    Check(stage.level.Load(LevelId::Dam), "corridor loads");
    stage.visibility.DetermineVisibleRooms(FrontView(1));

    Check(DrawnRooms(stage.visibility) == std::vector<int>({ 1, 2, 3 }), "rooms are found in portal order");
    Check(stage.visibility.RoomsDrawn()[0].depth == 0 && stage.visibility.RoomsDrawn()[1].depth == 1 &&
              stage.visibility.RoomsDrawn()[2].depth == 2,
          "a room's depth counts the portals crossed");

    ScreenRect bounds;
    Check(stage.visibility.GetRoomBounds2d(2, bounds) && bounds.left == 40 && bounds.right == 60,
          "a room seen through a portal is limited to the portal's area");
    Check(!stage.visibility.GetRoomBounds2d(7, bounds) && bounds.right == 0, "an undrawn room has no bounds");
    Check(stage.visibility.RoomRendered(3) && !stage.visibility.RoomNeighborToRendered(3), "drawn rooms are rendered");
}

void TestPortalCullsOffscreenRoom() {
    Corridor stage(500);
    stage.level.Load(LevelId::Dam);
    stage.visibility.DetermineVisibleRooms(FrontView(1));

    Check(DrawnRooms(stage.visibility) == std::vector<int>({ 1, 2 }), "a room behind an off-screen portal is skipped");
    Check(!stage.visibility.RoomRendered(3) && stage.visibility.RoomNeighborToRendered(3),
          "it is marked as next to a rendered room");
    Check(!stage.visibility.RoomNeighborToRendered(1), "a rendered room is not marked as a neighbor");

    stage.visibility.DetermineVisibleRooms(FrontView(2));
    Check(DrawnRooms(stage.visibility) == std::vector<int>({ 2 }),
          "the camera's own room comes first, portals facing away are not crossed, and the frame starts fresh");
}

void TestVisibilityCommands() {
    // DISABLE_ROOM 2, then end.
    Corridor disable(0, { { 0x24, 0 }, { 1, 2 } });
    SetRecordLength(disable.source.header, 0, 2);
    disable.level.Load(LevelId::Dam);
    disable.visibility.DetermineVisibleRooms(FrontView(1));
    Check(DrawnRooms(disable.visibility) == std::vector<int>({ 1, 3 }),
          "a disabled room is left out but the rooms behind it stay reachable");
    Check(disable.visibility.RoomDisabled(2) && disable.visibility.RoomRendered(2), "it is still flagged rendered");
    disable.visibility.DetermineVisibleRooms(FrontView(1));
    Check(disable.visibility.RoomDisabled(2), "the command runs again each frame");

    // FORCE_VISIBLE, then ADD_VISIBLE_ROOM 3.
    Corridor add(0, { { 0x1E, 0 }, { 0x20, 0 }, { 1, 3 } });
    SetRecordLength(add.source.header, 0, 1);
    SetRecordLength(add.source.header, 1, 2);
    add.level.Load(LevelId::Dam);
    add.visibility.DetermineVisibleRooms(FrontView(1));
    Check(add.visibility.CommandRooms() == std::vector<uint8_t>({ 3 }), "a command-added room is listed");
    add.visibility.ClearCommandRooms();
    Check(add.visibility.CommandRooms().empty(), "the list clears");
}

void TestStageWithoutPortals() {
    HeaderSpec spec;
    spec.rooms = { { true, false, {} }, { true, false, {} } };
    FakeSource source;
    source.header = BuildHeader(spec);
    source.geometries[1] = Vertices({ { -10, -10, -20 }, { 10, 10, -10 } });
    // Behind the camera.
    source.geometries[2] = Vertices({ { -10, -10, 10 }, { 10, 10, 20 } });
    BgLevel level(source);
    level.Load(LevelId::Dam);
    BgVisibility visibility(level);

    visibility.DetermineVisibleRooms(FrontView(1));
    Check(DrawnRooms(visibility) == std::vector<int>({ 1 }), "with no portals, every room in view is drawn");
    Check(visibility.RoomsDrawn()[0].next == 1, "and is marked as not reached through a portal");
}

} // namespace

void TestScreenMinMax() {
    ScreenRect full = ScreenMinMax({ 320, 240, 0, 0, 320, 240 });
    Check(full.left == 1 && full.top == 1 && full.right == 319 && full.bottom == 239,
          "the full frame is pulled in a pixel from each edge");

    ScreenRect letterbox = ScreenMinMax({ 320, 240, 0, 20, 320, 200 });
    Check(letterbox.top == 20 && letterbox.bottom == 220, "a view inside the frame keeps its own edges");

    ScreenRect oversized = ScreenMinMax({ 320, 240, -10, -10, 400, 300 });
    Check(oversized.left == 1 && oversized.top == 1 && oversized.right == 319 && oversized.bottom == 239,
          "a view past the frame is clamped to it");
}

void TestScissors() {
    const PlayerView view{ 0, 20, 320, 200 };

    Scissor all = ViewScissor(view);
    Check(all.left == 0 && all.top == 20 && all.right == 320 && all.bottom == 220, "the default box is the view");

    Scissor clipped = ClipScissor(view, -5, 0, 400, 300);
    Check(clipped.left == 0 && clipped.top == 20 && clipped.right == 320 && clipped.bottom == 220,
          "a box past the view is clipped to it");

    Scissor inside = ClipScissor(view, 10, 30, 100, 90);
    Check(inside.left == 10 && inside.top == 30 && inside.right == 100 && inside.bottom == 90,
          "a box inside the view is kept");

    Scissor rounded = BoundsScissor(view, { 10.9f, 30.9f, 100.1f, 90.1f });
    Check(rounded.left == 10 && rounded.top == 30 && rounded.right == 101 && rounded.bottom == 91,
          "a room's near edges truncate and its far edges round up");
}

void TestRoomSlots() {
    RoomSlots slots;
    slots.Reset(10);

    bool rebuild = false;
    int first = slots.Acquire(3, 1, rebuild);
    Check(first == 0 && rebuild, "a room's first draw takes the first free slot and builds its matrix");

    int again = slots.Acquire(3, 1, rebuild);
    Check(again == first && !rebuild, "it keeps the slot while the player stays in the same room");

    int other = slots.Acquire(4, 1, rebuild);
    Check(other == 1 && rebuild, "another room takes the next slot");

    int moved = slots.Acquire(3, 2, rebuild);
    Check(moved == 2 && rebuild, "a room seen from a new current room gets a fresh slot");

    slots.Tick();
    slots.Tick();
    int reused = slots.Acquire(5, 2, rebuild);
    Check(reused == 0 && rebuild, "slots unused for two ticks free up");
}

void TestRoomTransform() {
    float m[4][4];
    RoomTransform({ 100, 200, 300 }, 0.5f, { 10, 20, 30 }, m);
    Check(m[0][0] == 0.5f && m[1][1] == 0.5f && m[2][2] == 0.5f && m[3][3] == 1.0f && m[0][1] == 0.0f,
          "the matrix scales by the room scale");
    Check(m[3][0] == 40 && m[3][1] == 80 && m[3][2] == 120,
          "the room's scaled position is taken relative to the current room");
}

void TestSetRoomDepth() {
    Corridor stage(0);
    stage.level.Load(LevelId::Dam);
    stage.visibility.DetermineVisibleRooms(FrontView(1));
    stage.visibility.SetRoomDepth(3, 0);
    Check(stage.visibility.RoomsDrawn()[2].room == 3 && stage.visibility.RoomsDrawn()[2].depth == 0,
          "a drawn room's depth can be reset");
}

// Two stacked rooms joined by a floor portal (normal along y) at y = -10 and
// a wall portal (normal along z) at z = -25 beside it.
struct Stack {
    Stack() : level(source), visibility(level) {
        HeaderSpec spec;
        spec.rooms = { { true, false, {} }, { true, false, {} } };
        PortalSpec floor = { 1, 2, 0, 0, { { -5, -10, -5 }, { -5, -10, 5 }, { 5, -10, 5 }, { 5, -10, -5 } } };
        spec.portals = { floor, ZPortal(1, 2, -25, -5, 5) };
        source.header = BuildHeader(spec);
        source.geometries[1] = Vertices({ { -10, -10, -30 }, { 10, 10, 10 } });
        source.geometries[2] = Vertices({ { -10, -30, -30 }, { 10, -10, 10 } });
        scale = LevelEntryAt(FindLevelIndex(LevelId::Dam)).scale;
        level.Load(LevelId::Dam);
    }

    // A point in room units, as the player's position is stored.
    Vec3 At(float x, float y, float z) const {
        return { x / scale, y / scale, z / scale };
    }

    FakeSource source;
    BgLevel level;
    BgVisibility visibility;
    float scale = 1;
};

void TestPortalCrossing() {
    Stack stack;
    Check(stack.level.PortalCrossing(0, stack.At(0, -5, 0), stack.At(0, -15, 0)) != 0,
          "a segment through the opening crosses");
    Check(stack.level.PortalCrossing(0, stack.At(0, -5, 0), stack.At(0, -15, 0)) !=
              stack.level.PortalCrossing(0, stack.At(0, -15, 0), stack.At(0, -5, 0)),
          "which side it started on is reported");
    Check(stack.level.PortalCrossing(0, stack.At(0, -5, 0), stack.At(0, -8, 0)) == 0, "stopping short does not cross");
    Check(stack.level.PortalCrossing(0, stack.At(20, -5, 0), stack.At(20, -15, 0)) == 0,
          "crossing the plane outside the polygon does not count");
    float mid = 0;
    stack.level.PortalCrossing(0, stack.At(0, -5, 0), stack.At(0, -15, 0), &mid);
    Check(std::fabs(mid) < 1e-4f, "the midpoint of a symmetric crossing is on the plane");
}

void TestUpdateFollowsFloorPortals() {
    Stack stack;
    VisibilityView view = FrontView(1);
    view.position = stack.At(0, -15, 0);
    Check(stack.visibility.Update(view, stack.At(0, -5, 0), CameraMode::FirstPerson) == 2,
          "a move through a floor portal ends in the other room");
    Check(!stack.visibility.RoomsDrawn().empty() && stack.visibility.RoomsDrawn()[0].room == 2,
          "the visibility pass starts from that room");

    view.currentRoom = 2;
    view.position = stack.At(0, -5, 0);
    Check(stack.visibility.Update(view, stack.At(0, -15, 0), CameraMode::FirstPerson) == 1, "and back again");

    view.currentRoom = 1;
    view.position = stack.At(0, -5, -30);
    Check(stack.visibility.Update(view, stack.At(0, -5, -20), CameraMode::FirstPerson) == 1,
          "a wall portal is left to the player's tile");
}

void TestUpdateRoomBudget() {
    Stack stack;
    VisibilityView view = FrontView(1);
    view.position = stack.At(0, 0, 0);
    stack.visibility.Update(view, view.position, CameraMode::FirstPerson);
    Check(stack.level.RoomLoadBudget() == 3, "first person loads a few rooms a frame");
    stack.visibility.Update(view, view.position, CameraMode::Intro);
    Check(stack.level.RoomLoadBudget() == 0xC8, "the intro loads whatever it can see");
}

class FakeDemoSource final : public DemoSource {
  public:
    std::optional<std::string> LoadTable() override {
        return "Dam_1\t0\nTrain\t3\nSilo_1\t0\n";
    }
    std::optional<std::vector<uint8_t>> LoadDemo(const std::string& name) override {
        if (name != "Train") {
            return std::nullopt;
        }
        std::vector<uint8_t> file(0xE8, 0);
        PutU32(file, 0x10, 25);
        PutU32(file, 0x14, 2);
        PutU32(file, 0x88, 1);
        return file;
    }
};

void TestAttractDemo() {
    const auto table = ParseRamromTable("Dam_1\t0\nTrain\t3\n");
    Check(table.size() == 2 && table[1].name == "Train" && table[1].locked == 3, "the table parses in order");

    Check(SelectRamrom(table, 0, 7) == 0, "a demo locked behind a later stage is not offered");
    Check(SelectRamrom(table, 3, 7) == 1 && SelectRamrom(table, 3, 4) == 0,
          "the pick is the random number modulo the unlocked count");
    Check(!SelectRamrom({ { "Late", 5 } }, 0, 1).has_value(), "nothing unlocked, nothing to play");

    std::vector<uint8_t> file(0xE8, 0);
    PutU32(file, 0x10, 33);
    PutU32(file, 0x14, 1);
    PutU32(file, 0x88, 2);
    PutU32(file, 0x94, 1);
    PutU32(file, 0x00, 0x1);
    PutU32(file, 0x04, 0xCE035AF9u);
    PutU32(file, 0x08, 0xAB8D9F77u);
    PutU32(file, 0x0C, 0x81280783u);
    // The save file's options: a wide screen and the 2.1 control style.
    file[0x28] = 0x04;
    file[0x29] = 0x7A;
    const auto header = ParseDemoHeader(file);
    Check(header && header->stage == LevelId::Dam && header->difficulty == 1 && header->slot == 2,
          "the header names stage, difficulty and slot");
    Check(header && header->randomSeed == 0x1CE035AF9ull && header->chrObjRandomSeed == 0xAB8D9F7781280783ull,
          "and the two random seeds");
    Check(header && header->options == 0x047A, "and the options of the save file it carries");
    Check(!ParseDemoHeader(std::vector<uint8_t>(8)).has_value(), "a truncated file has no header");

    FakeDemoSource source;
    const auto demo = LoadAttractDemo(source, 1);
    Check(demo && demo->name == "Train" && demo->header.stage == LevelId::Train && demo->header.difficulty == 2,
          "a demo loads by table index");
    Check(!LoadAttractDemo(source, 3).has_value() && !LoadAttractDemo(source, 0).has_value(),
          "an index past the table, or a demo with no file, does not load");
    const auto picked = SelectAttractDemo(source, 3, 1);
    Check(picked && picked->name == "Train", "the selection loads the demo it picked");
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

// A setup file with two pads, an intro section for normal play and one for
// demo slot 1, three cameras' worth of records, and a propdef section.
std::vector<uint8_t> BuildSetupFile() {
    std::vector<uint8_t> file(0x400);
    PutWords(file, 0, { 0, 0, 0x110, 0x200, 0, 0, 0x28, 0x80, 0xC4, 0 });
    PutPad(file, 0x28, { 10, 20, 30 }, { 1, 0, 0 }, 0x100);
    PutPad(file, 0x28 + 0x2C, { 1, 2, 3 }, { 0, 0, 1 }, 0x106);
    PutWords(file, 0xC4, { 0x100, 0x106, 0 });
    std::memcpy(&file[0x100], "tileA", 6);
    std::memcpy(&file[0x106], "tileB", 6);
    size_t at = 0x110;
    auto record = [&](std::initializer_list<int32_t> words) {
        PutWords(file, at, words);
        at += words.size() * 4;
    };
    record({ 0, 1, 0 });
    record({ 0, 0, 1 });
    record({ 1, 5, -1, 0 });
    record({ 1, 8, 8, 1 });
    record({ 2, 1, 100, 0 });
    record({ 6, 150, 250, 350, 0x10000, 0x8000, 7, 0x00010002, 0, 0 });
    record({ 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    record({ 7, 14, 30 });
    record({ 9 });

    // The propdefs: a door scale, three props (each 32 words), a link and a
    // guard, then the end record.
    at = 0x200;
    record({ 2, 0x18000 });
    for (const int32_t flags2 : { 0, 0x10, 0x400000 }) {
        record({ 0x01002003, 0x00330005, 0x1234, flags2 });
        at += 28 * 4;
    }
    record({ 0x0E, 0, 0 });
    record({ 9, 0, 0, 0, 0, 0, 0 });
    record({ 0x30 });
    return file;
}

void TestStageSetup() {
    Check(SetupName(LevelId::Dam) != nullptr && std::string(SetupName(LevelId::Dam)) == "setupdamZ",
          "a stage names its setup file");
    Check(SetupName(LevelId::Title) == nullptr, "the title has no setup file");

    std::string error;
    const auto setup = StageSetup::Parse(BuildSetupFile(), 2.0f, &error);
    Check(setup.has_value(), "a setup file parses");
    if (!setup) {
        return;
    }
    Check(setup->Pads().size() == 2 && setup->Pads()[0].tileName == "tileA" && setup->Pads()[1].tileName == "tileB",
          "pads end at the empty record");
    Check(setup->Pads()[0].position == Vec3({ 20, 40, 60 }) && setup->Pads()[0].look == Vec3({ 1, 0, 0 }),
          "pad positions are scaled and directions are not");
    Check(setup->BoundPads().empty() && setup->PadNames() == std::vector<std::string>({ "tileA", "tileB" }) &&
              setup->BoundPadNames().empty(),
          "the name lists end at 0 and an absent list is empty");
    Check(setup->Intro().size() == 8, "the intro ends at the end record");
    const auto* camera = std::get_if<IntroCamera>(&setup->Intro()[5]);
    Check(camera && camera->position == Vec3({ 1.5f, 2.5f, 3.5f }) && camera->yaw == 1.0f && camera->pitch == 0.5f &&
              camera->pad == 7 && camera->caption[1] == 2 && camera->caption[0] == 1,
          "camera records convert their fixed-point fields");

    const auto& defs = setup->PropDefs();
    Check(defs.size() == 6 && defs[0].Is(PropDefType::DoorScale) && defs[1].offset == 0x208 && defs[1].words == 32 &&
              defs[4].Is(PropDefType::Link) && defs[5].Is(PropDefType::Guard),
          "propdefs are walked by their sizes up to the end record");
    Check(Near(setup->DoorScale(), 1.5f), "the door scale record sets the scale");
    Check(defs[1].extraScale == 0x100 && defs[1].state == 0x20 && defs[1].model == 0x33 && defs[1].pad == 5 &&
              defs[1].flags == 0x1234,
          "an object record's fields are read");
    const PropCounts counts = setup->Counts();
    Check(counts.objects == 3 && counts.guards == 1 && counts.links == 1 && counts.animatedObjects == 1,
          "the counts size the object, guard and item tables");
    Check(setup->Count(PropDefType::Prop) == 3 && setup->Count(PropDefType::Door) == 0, "load_proptype counts a type");
    Check(StageSetup::LoadsObject(defs[1], 0, 1) && !StageSetup::LoadsObject(defs[2], 0, 1) &&
              StageSetup::LoadsObject(defs[2], 1, 1),
          "an object can opt out of one difficulty");
    Check(StageSetup::LoadsObject(defs[3], 0, 1) && !StageSetup::LoadsObject(defs[3], 0, 2), "or out of two players");

    std::vector<uint8_t> truncated = BuildSetupFile();
    truncated.resize(0x120);
    Check(!StageSetup::Parse(truncated, 1.0f, &error).has_value(), "a truncated setup file is rejected");
    std::vector<uint8_t> unended = BuildSetupFile();
    unended.resize(0x300);
    Check(!StageSetup::Parse(unended, 1.0f, &error).has_value(), "a propdef section with no end record is rejected");
}

void TestIntroSection() {
    auto setup = StageSetup::Parse(BuildSetupFile(), 2.0f, nullptr);
    if (!setup) {
        Check(false, "the intro test's setup parses");
        return;
    }
    // A flat floor at stan height 2, which is world height 4 at scale 0.5; the
    // pads' names match no tile, so each is found by position.
    Stan stan;
    stan.Load(StanFixture::MakeFloor(100, 2, 0, 3), 0.5f);
    const auto counts = setup->ResolveStanTiles(stan);
    Check(setup->Pads()[0].stanTile == 0 && setup->Pads()[1].stanTile == 0, "pads find their stan tiles");
    Check(counts.named == 0 && counts.moved == 2 && counts.missing == 0,
          "the resolution counts how each pad was found");
    const auto random = [] { return uint32_t{ 1 }; };

    IntroSetup normal = LoadIntroSection(*setup, 0, random);
    Check(normal.startPads == std::vector<size_t>({ 1 }), "normal play takes the slot 0 spawn");
    Check(normal.inventory.HasItem(5) && !normal.inventory.HasDual(8, 8) && normal.inventory.HasItem(kItemFist),
          "normal play gets its items and the fist");
    Check(normal.startingWeaponRight == 5 && normal.startingWeaponLeft == kItemUnarmed,
          "the first item is the starting weapon");
    Check(normal.ammo.size() == 1 && normal.ammo[0].amount == 100, "ammo grants are kept");
    Check(normal.cameras.size() == 2 && normal.selectedCamera == 0u,
          "the random pick counts back from the last camera");
    Check(normal.watchTime == 30 * 3600 + 2 * 216000, "the watch time is minutes plus hours modulo 12");

    IntroSetup demo = LoadIntroSection(*setup, 1, random);
    Check(demo.startPads == std::vector<size_t>({ 0 }) && demo.inventory.HasDual(8, 8) && !demo.inventory.HasItem(5),
          "a demo takes its own spawn and items");
    Check(demo.startingWeaponRight == 8 && demo.startingWeaponLeft == 8, "a dual item sets both hands");

    const PlayerSpawn spawn = PlacePlayer(*setup, normal, stan, 1.0f);
    Check(spawn.position == Vec3({ 2, 5, 6 }) && spawn.stanHeight == 4.0f && spawn.stanTile == 0,
          "the spawn is the pad, one unit above the floor under it");
    Check(Near(spawn.theta, 360.0f) && Near(spawn.look[2], 1.0f), "the spawn faces the pad's look direction");
    Check(spawn.eyeHeight == 175.0f && spawn.EyePosition() == Vec3({ 2, 179, 6 }), "life start sets the eye height");

    const PlayerSpawn demoSpawn = PlacePlayer(*setup, demo, stan, 1.0f);
    Check(Near(demoSpawn.look[0], 1.0f) && Near(demoSpawn.look[2], 0.0f), "the look angle is measured from +z");

    // No spawn record for this slot: the player goes where a circle of radius
    // 30 fits nearest the origin.
    IntroSetup noPads = LoadIntroSection(*setup, 9, random);
    Check(noPads.startPads.empty(), "a slot with no spawn record has no start pad");
    const PlayerSpawn found = PlacePlayer(*setup, noPads, stan, 1.0f);
    Check(found.stanTile == 0 || found.stanTile == 1, "a stage without start pads searches the stan");
    Check(found.position[0] == stan.TileMidPoint(found.stanTile)[0] && found.stanHeight == 4.0f &&
              found.position[1] == 5.0f,
          "the search ends at a tile's centre, above the floor");
}

void TestInventory() {
    Inventory inventory;
    Check(inventory.AddItem(9) && inventory.AddItem(3) && inventory.AddDoubles(3, 3) && inventory.AddItem(6),
          "items add");
    Check(!inventory.AddItem(9) && !inventory.AddDoubles(3, 3), "an item already held does not add");
    const auto& items = inventory.Items();
    Check(items.size() == 4 && items[0].right == 3 && !items[0].IsDual() && items[1].right == 3 && items[1].IsDual() &&
              items[2].right == 6 && items[3].right == 9,
          "the inventory is sorted by weapon id, a single before its dual");
}

void TestInventoryProps() {
    Inventory inventory;
    inventory.AddItem(9);
    inventory.AddProp(30, 5);
    inventory.AddItem(3);
    inventory.AddProp(31, 6);
    const auto& items = inventory.Items();
    Check(items.size() == 4 && items[0].right == 3 && items[1].right == 9 && items[2].IsProp() && items[3].IsProp(),
          "kept props sort after every weapon");
    Check(items[2].prop == 30 && items[3].prop == 31, "and among themselves in the order they were kept");
    Check(!inventory.HasItem(-1) && !items[2].IsDual(), "a prop is neither a weapon nor a pair");

    inventory.AddTextOverride({ 5, 27, StringId(TextBank::PropObj, 0x3C) });
    inventory.AddTextOverride({ -1, 4, StringId(TextBank::PropObj, 0x20) });
    Check(inventory.TextByObject(5) && inventory.TextByObject(5)->weapon == 27 && !inventory.TextByObject(6),
          "an override is found by the object it names");
    Check(inventory.TextByWeapon(4) && inventory.TextByWeapon(4)->pickupText == StringId(TextBank::PropObj, 0x20) &&
              !inventory.TextByWeapon(27),
          "or by the weapon id when it names no object");
    Check(inventory.TextIndex(0) == 3 && inventory.TextIndex(2) == 27 && inventory.TextIndex(3) == kItemUnarmed &&
              inventory.TextIndex(9) == kItemUnarmed,
          "a kept prop displays as its override's weapon, and none as unarmed");
}

void TestLang() {
    Check(Lang::StageBank(LevelId::Dam) == 11 && Lang::StageBank(LevelId::Bunker1) == 30 &&
              Lang::StageBank(LevelId::Title) == -1,
          "a stage names its own text bank");
    Check(Lang::BankResource(TextBank::Gun) == "obseg/text/LgunE" && Lang::BankResource(11) == "obseg/text/LdamE" &&
              Lang::BankResource(0).empty() && Lang::BankResource(45).empty(),
          "and banks have resource names");

    Lang lang;
    lang.SetBank(TextBank::PropObj, { "first", "", "third" });
    Check(lang.Get(StringId(TextBank::PropObj, 0)) == "first" && lang.Get(StringId(TextBank::PropObj, 2)) == "third",
          "a string is found by bank and slot");
    Check(lang.Get(StringId(TextBank::PropObj, 1)).empty() && lang.Get(StringId(TextBank::PropObj, 3)).empty() &&
              lang.Get(StringId(TextBank::Gun, 0)).empty(),
          "a slot with no text, and a bank that isn't loaded, read as empty");
}

// The pickup text bank with a token in each slot, so the wording comes from
// the slots the text picks and not from words.
Lang TokenLang() {
    std::vector<std::string> words(0x40);
    for (size_t i = 0; i < words.size(); i++) {
        words[i] = "<" + std::to_string(i) + ">";
    }
    Lang lang;
    lang.SetBank(TextBank::PropObj, words);
    return lang;
}

void TestPickupText() {
    const Lang lang = TokenLang();
    // "Picked up" + "some" + "ammo" + the full stop.
    Check(AmmoCollectedText(lang, AmmoKind::Ammo9mm, 10) == "<0><1><5>.\n", "9mm ammo is 'some ammo'");
    Check(AmmoCollectedText(lang, AmmoKind::Knife, 1) == "<0><2><15><16>.\n" &&
              AmmoCollectedText(lang, AmmoKind::Knife, 3) == "<0><1><15><17>.\n",
          "a throwing knife, or some throwing knives");
    Check(AmmoCollectedText(lang, AmmoKind::Dynamite, 1) == "<0><2><25><24>.\n", "a stick of dynamite");
    Check(AmmoCollectedText(lang, AmmoKind::GrenadeRound, 3) == "<0><1><10><18>.\n",
          "some grenade rounds, with a plural ending");
    Check(AmmoCollectedText(lang, AmmoKind::BombCase, 1) == "<0><3><21>.\n" &&
              AmmoCollectedText(lang, AmmoKind::GoldenEyeKey, 1) == "<0><4><29>.\n",
          "an explosive case, and the GoldenEye key");

    const auto table = WeaponTable::Parse(
        "item\t0\tITEM_UNARMED\t0\t0\t0\t0x110\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
        "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n");
    Check(WeaponPickupText(lang, *table, 4) == "<0><33>" && WeaponPickupText(lang, *table, 88).size() > 0 &&
              WeaponPickupText(lang, *table, 81) == "<0><56>" && WeaponPickupText(lang, *table, 999) == "<0><59>",
          "a gun, a keycard and an unknown weapon are named by their slots");
}

void TestDemoPlayback() {
    std::vector<uint8_t> file(0xE8, 0);
    PutU32(file, 0x18, 1);
    PutU32(file, 0x7C, 1000);
    auto packet = [&](uint8_t frames, std::vector<std::array<uint8_t, 4>> samples, bool badCheck = false) {
        uint8_t sum = static_cast<uint8_t>(frames + samples.size() + 7);
        file.push_back(frames);
        file.push_back(static_cast<uint8_t>(samples.size()));
        file.push_back(7);
        const size_t checkAt = file.size();
        file.push_back(0);
        for (const auto& s : samples) {
            for (uint8_t byte : s) {
                file.push_back(byte);
                sum = static_cast<uint8_t>(sum + byte);
            }
        }
        file[checkAt] = badCheck ? static_cast<uint8_t>(sum + 1) : sum;
    };
    packet(2, { { 10, 0xFB, 0x00, 0x80 }, { 20, 30, 0x20, 0x00 } });
    packet(1, { { 0, 0, 0, 0 } });
    packet(1, { { 1, 1, 1, 1 } }, true);
    auto playback = DemoPlayback::Parse(file);
    Check(playback && playback->TotalTime() == 1000, "a demo parses");
    if (!playback) {
        return;
    }
    const auto first = playback->Next();
    Check(first && first->speedFrames == 2 && first->samples.size() == 2, "a packet holds its frame's samples");
    const MoveInput last = first->Input();
    Check(last.stickX == 20 && last.stickY == 30 && last.buttons == 0x0020, "the frame reads its last sample");
    Check(first->samples[0].stickY == -5 && first->samples[0].buttons == 0x8000,
          "sticks are signed, buttons two bytes");
    Check(playback->Next().has_value(), "the next packet follows");
    Check(!playback->Next().has_value(), "a packet with a bad checksum ends the demo");
    Check(!DemoPlayback::Parse(std::vector<uint8_t>(8)).has_value(), "a truncated file does not parse");
}

int main() {
    TestDemoPlayback();
    TestStageSetup();
    TestIntroSection();
    TestInventory();
    TestInventoryProps();
    TestLang();
    TestPickupText();
    TestLevelTable();
    TestHeaderParse();
    TestHeaderRejects();
    TestLoadComputesBounds();
    TestRoomBoundsFromStan();
    TestLoadFailures();
    TestRoomLifecycle();
    TestPortalPreparation();
    TestSpecialPortals();
    TestRects();
    TestScreenMinMax();
    TestScissors();
    TestRoomSlots();
    TestRoomTransform();
    TestSetRoomDepth();
    TestVisibleThroughPortals();
    TestPortalCullsOffscreenRoom();
    TestVisibilityCommands();
    TestStageWithoutPortals();
    TestPortalCrossing();
    TestUpdateFollowsFloorPortals();
    TestUpdateRoomBudget();
    TestAttractDemo();
    TestRealFiles();
    return gFailures == 0 ? 0 : 1;
}
