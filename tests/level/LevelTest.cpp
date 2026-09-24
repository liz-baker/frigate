#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "level/BgHeader.h"
#include "level/BgLevel.h"
#include "level/BgView.h"
#include "level/BgVisibility.h"
#include "level/LevelInfo.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void PutU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value >> 24);
    bytes[at + 1] = static_cast<uint8_t>(value >> 16);
    bytes[at + 2] = static_cast<uint8_t>(value >> 8);
    bytes[at + 3] = static_cast<uint8_t>(value);
}

void PutF32(std::vector<uint8_t>& bytes, size_t at, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(bytes, at, bits);
}

uint32_t Addr(size_t offset) {
    return 0x0F000000u | static_cast<uint32_t>(offset);
}

struct PortalSpec {
    uint8_t room1, room2, control1, control2;
    std::vector<Vec3> points;
};

struct HeaderSpec {
    // (vertexBlob present, secondary present, position)
    struct Room {
        bool vertices;
        bool secondary;
        Vec3 position;
    };
    std::vector<Room> rooms;
    std::vector<PortalSpec> portals;
    // Visibility records as (type, argument); the argument of type 100 is
    // the index of the portal whose address it names.
    std::vector<std::pair<uint8_t, int>> visibility;
    uint32_t firstWord = 0;
};

// A header region laid out like a bg file's: five header words, the room
// table (placeholder, rooms, end entry, null entry), the portal table, the
// portals' points, then the visibility records.
std::vector<uint8_t> BuildHeader(const HeaderSpec& spec) {
    const size_t roomTable = 0x14;
    const size_t entryCount = spec.rooms.size() + 3;
    const size_t portalTable = roomTable + entryCount * 0x18;
    size_t cursor = portalTable + (spec.portals.size() + 1) * 8;
    std::vector<size_t> pointsAt;
    for (const PortalSpec& portal : spec.portals) {
        pointsAt.push_back(cursor);
        cursor += 4 + portal.points.size() * 12;
    }
    const size_t visibilityAt = cursor;
    cursor += (spec.visibility.size() + 1) * 8;

    std::vector<uint8_t> bytes(cursor, 0);
    PutU32(bytes, 0, spec.firstWord);
    PutU32(bytes, 4, Addr(roomTable));
    PutU32(bytes, 8, Addr(portalTable));
    PutU32(bytes, 12, spec.visibility.empty() ? 0 : Addr(visibilityAt));
    PutU32(bytes, 16, 0);

    size_t entry = roomTable + 0x18;
    for (const auto& room : spec.rooms) {
        PutU32(bytes, entry, room.vertices ? Addr(0x1000) : 0);
        PutU32(bytes, entry + 4, Addr(0x2000));
        PutU32(bytes, entry + 8, room.secondary ? Addr(0x3000) : 0);
        for (int axis = 0; axis < 3; axis++) {
            PutF32(bytes, entry + 12 + axis * 4, room.position[axis]);
        }
        entry += 0x18;
    }
    // The end entry: not a room, but its primary pointer is non-null.
    PutU32(bytes, entry, Addr(0x4000));
    PutU32(bytes, entry + 4, Addr(0x4000));

    for (size_t i = 0; i < spec.portals.size(); i++) {
        const PortalSpec& portal = spec.portals[i];
        size_t at = portalTable + i * 8;
        PutU32(bytes, at, Addr(pointsAt[i]));
        bytes[at + 4] = portal.room1;
        bytes[at + 5] = portal.room2;
        bytes[at + 6] = portal.control1;
        bytes[at + 7] = portal.control2;
        bytes[pointsAt[i]] = static_cast<uint8_t>(portal.points.size());
        for (size_t p = 0; p < portal.points.size(); p++) {
            for (int axis = 0; axis < 3; axis++) {
                PutF32(bytes, pointsAt[i] + 4 + p * 12 + axis * 4, portal.points[p][axis]);
            }
        }
    }

    for (size_t i = 0; i < spec.visibility.size(); i++) {
        size_t at = visibilityAt + i * 8;
        bytes[at] = spec.visibility[i].first;
        uint32_t argument = static_cast<uint32_t>(spec.visibility[i].second);
        if (spec.visibility[i].first == 100) {
            argument = Addr(pointsAt[spec.visibility[i].second]);
        }
        PutU32(bytes, at + 4, argument);
    }
    return bytes;
}

// A square portal of side 2 in the plane x = `x`, facing +x.
PortalSpec WallPortal(uint8_t room1, uint8_t room2, float x) {
    return { room1, room2, 0x01, 0, { { x, 0, 0 }, { x, 0, 2 }, { x, 2, 2 }, { x, 2, 0 } } };
}

// Points ordered so the plane's normal is +x for WallPortal-like geometry:
// (p0-p1) x (p2-p1).
float NormalX(const PortalSpec& portal) {
    const auto& p0 = portal.points[0];
    const auto& p1 = portal.points[1];
    const auto& p2 = portal.points[2];
    float ay = p0[1] - p1[1], az = p0[2] - p1[2];
    float by = p2[1] - p1[1], bz = p2[2] - p1[2];
    return ay * bz - az * by;
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

// A source that serves a built header and canned rooms.
class FakeSource final : public BgSource {
  public:
    std::optional<std::vector<uint8_t>> LoadHeader(const LevelEntry&) override {
        if (header.empty()) {
            return std::nullopt;
        }
        return header;
    }

    std::optional<RoomGeometry> LoadRoom(const LevelEntry&, int room, const BgRoomEntry&) override {
        loads[room]++;
        if (failRoom == room) {
            return std::nullopt;
        }
        RoomGeometry geometry = geometries.count(room) ? geometries[room] : RoomGeometry{};
        geometry.resident = std::make_shared<int>(room);
        residentWatch[room] = geometry.resident;
        return geometry;
    }

    std::vector<uint8_t> header;
    std::map<int, RoomGeometry> geometries;
    std::map<int, int> loads;
    std::map<int, std::weak_ptr<void>> residentWatch;
    int failRoom = -1;
};

RoomGeometry Vertices(std::vector<std::array<int16_t, 3>> positions) {
    RoomGeometry geometry;
    geometry.vertexPositions = std::move(positions);
    geometry.primaryList = "primary";
    return geometry;
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

int main() {
    TestLevelTable();
    TestHeaderParse();
    TestHeaderRejects();
    TestLoadComputesBounds();
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
    TestRealFiles();
    return gFailures == 0 ? 0 : 1;
}
