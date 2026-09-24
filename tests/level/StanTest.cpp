#include <cmath>
#include <cstdio>
#include <vector>

#include "StanFixture.h"
#include "level/Stan.h"
#include "level/StanData.h"
#include "level/StanIntersection.h"

using namespace Frigate::Level;
using namespace StanFixture;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) < tolerance;
}

void PutU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void PutU32(std::vector<uint8_t>& bytes, uint32_t value) {
    PutU16(bytes, static_cast<uint16_t>(value >> 16));
    PutU16(bytes, static_cast<uint16_t>(value));
}

void PutTile(std::vector<uint8_t>& bytes, uint32_t id, uint8_t room, uint16_t tail, int points, uint16_t link) {
    PutU32(bytes, (id << 8) | room);
    PutU16(bytes, 0x0FFF);
    PutU16(bytes, tail);
    for (int i = 0; i < points; i++) {
        PutU16(bytes, static_cast<uint16_t>(i));
        PutU16(bytes, static_cast<uint16_t>(-2));
        PutU16(bytes, static_cast<uint16_t>(i * 3));
        PutU16(bytes, i == 0 ? link : 0xFFFF);
    }
}

void TestParsePayload() {
    std::vector<uint8_t> bytes;
    PutU32(bytes, 2);
    PutTile(bytes, 0x08CC32, 0x88, 0x3012, 3, 1);
    PutTile(bytes, 0x08CC31, 0x89, 0x4013, 4, 0);

    StanData data;
    Check(ParseStanPayload(bytes.data(), bytes.size(), &data) && data.tiles.size() == 2, "a payload parses");
    if (data.tiles.size() != 2) {
        return;
    }
    const StanTile& tile = data.tiles[0];
    Check(tile.id == 0x08CC32 && tile.room == 0x88 && tile.mid == 0x0FFF && tile.PointCount() == 3, "tile header");
    Check(tile.Triple(0) == 0 && tile.Triple(1) == 1 && tile.Triple(2) == 2, "the tail names the plane's points");
    Check(tile.points[1].x == 1 && tile.points[1].y == -2 && tile.points[1].z == 3, "points are signed");
    Check(tile.points[0].link == 1 && tile.points[1].link == kNoTile, "links are tile indices, 0xFFFF is none");
    Check(data.tiles[1].PointCount() == 4 && data.tiles[1].Triple(2) == 3, "a quad's tail");

    bytes.pop_back();
    Check(!ParseStanPayload(bytes.data(), bytes.size(), &data), "a truncated payload is rejected");

    std::vector<uint8_t> badLink;
    PutU32(badLink, 1);
    PutTile(badLink, 1, 1, 0x3012, 3, 5);
    Check(!ParseStanPayload(badLink.data(), badLink.size(), &data), "a link past the last tile is rejected");

    std::vector<uint8_t> badCount;
    PutU32(badCount, 1);
    PutTile(badCount, 1, 1, 0x2012, 2, 0xFFFF);
    Check(!ParseStanPayload(badCount.data(), badCount.size(), &data),
          "a tile with fewer than three points is rejected");

    std::vector<uint8_t> hugeCount;
    PutU32(hugeCount, 0x7FFFFFFF);
    Check(!ParseStanPayload(hugeCount.data(), hugeCount.size(), &data),
          "a tile count the payload cannot hold is rejected");
}

void TestRoomTables() {
    auto data = MakeFloor(100, 0, 10, 3);
    data->tiles.push_back(
        MakeTile(0x80070B, 5, { { { -4, 2, 7 }, { 9, -6, 8 }, { 0, 0, 30 } } }, { kNoTile, kNoTile, kNoTile }));

    Stan stan;
    Check(stan.Load(data, 1.0f), "a stan loads");
    Check(stan.RoomCount() == 6, "the room count is one past the highest room");
    Check(stan.FirstTileInRoom(3) == 0 && stan.FirstTileInRoom(5) == 2 && stan.FirstTileInRoom(4) == kNoTile,
          "each room's first tile");
    const Stan::RoomBounds& bounds = stan.Bounds(3);
    Check(bounds.min == std::array<int16_t, 3>({ 0, 0, 0 }) && bounds.max == std::array<int16_t, 3>({ 100, 10, 100 }),
          "a room's bounds cover its tiles' points");
    Check(stan.Bounds(5).min == std::array<int16_t, 3>({ -4, -6, 7 }), "another room's bounds");
    Check(stan.TileRoom(2) == 5, "getTileRoom");

    auto tooHigh = MakeFloor(10, 0, 0, static_cast<uint8_t>(Stan::kMaxRooms));
    Check(!stan.Load(tooHigh, 1.0f) && !stan.Loaded(), "a tile in a room past the tables is rejected");
}

void TestMatchTileName() {
    auto data = MakeFloor(100, 0, 0, 3);
    data->tiles.push_back(
        MakeTile(0x80070B, 5, { { { 0, 0, 0 }, { 0, 0, 5 }, { 5, 0, 5 } } }, { kNoTile, kNoTile, kNoTile }));
    Stan stan;
    stan.Load(data, 1.0f);

    Check(stan.MatchTileName("p5a1") == 0 && stan.MatchTileName("p5a2") == 1, "a name finds its tile");
    Check(stan.MatchTileName("q7b3") == 2, "a q name sets the type bit");
    Check(stan.MatchTileName("p5a") == kNoTile, "a name with no sub-triangle is sub-triangle 0");
    Check(stan.MatchTileName("p5a0") == kNoTile, "sub-triangle '0' is not converted, so the game rejects it");
    Check(stan.MatchTileName("") == kNoTile && stan.MatchTileName("x5a1") == kNoTile &&
              stan.MatchTileName("p5A1") == kNoTile && stan.MatchTileName("p5a12") == kNoTile &&
              stan.MatchTileName("pa1") == kNoTile,
          "malformed names match nothing");
}

void TestHeights() {
    Stan stan;
    stan.Load(MakeFloor(100, 0, 10, 3), 1.0f);
    Check(Near(stan.PositionY(0, 50, 75), 5.0f) && Near(stan.PositionY(1, 80, 20), 8.0f),
          "the height is the plane under the position");
    Check(Near(stan.PositionY(0, 0, 50), 0.0f), "the low edge");

    Stan scaled;
    scaled.Load(MakeFloor(100, 20, 0, 3), 0.5f);
    Check(Near(scaled.PositionY(0, 40, 60), 40.0f), "world heights are stan heights over the scale");

    Check(stan.PointInsideTriangle(0, 50, 75) && !stan.PointInsideTriangle(0, 75, 50) &&
              stan.PointInsideTriangle(1, 75, 50),
          "the inside test follows the triangle's edges");
    Check(scaled.PointInsideTriangle(0, 80, 120) && !scaled.PointInsideTriangle(0, 120, 80),
          "the inside test takes world positions");
    Check(!stan.TileHasZeroArea(0), "a triangle has area");

    auto flat = std::make_shared<StanData>();
    flat->tiles.push_back(
        MakeTile(1, 1, { { { 0, 0, 0 }, { 5, 0, 5 }, { 10, 0, 10 } } }, { kNoTile, kNoTile, kNoTile }));
    Stan line;
    line.Load(flat, 1.0f);
    Check(line.TileHasZeroArea(0), "collinear points have no area");

    const Vec3 mid = stan.TileMidPoint(0);
    Check(Near(mid[0], 100.0f / 3.0f) && Near(mid[1], 10.0f / 3.0f) && Near(mid[2], 200.0f / 3.0f),
          "the mid point averages the plane's points");
}

void TestWalk() {
    Stan stan;
    stan.Load(MakeFloor(100, 0, 10, 3), 1.0f);

    int tile = 0;
    Check(stan.WalkTiles(&tile, 25, 50, 75, 25) && tile == 1, "a walk across the diagonal ends on the far tile");
    tile = 1;
    Check(stan.WalkTiles(&tile, 75, 25, 25, 50) && tile == 0, "and back");
    tile = 0;
    Check(stan.WalkTiles(&tile, 10, 20, 30, 80) && tile == 0, "a walk inside a tile stays on it");

    tile = 1;
    Check(!stan.WalkTiles(&tile, 75, 25, 150, 25) && tile == 1, "a walk out of the tiles fails");
    Check(stan.LastCollision().tile == 1 && stan.LastCollision().point == 1,
          "and records the tile and edge it left through");

    Stan scaled;
    scaled.Load(MakeFloor(100, 0, 10, 3), 0.5f);
    tile = 0;
    Check(scaled.WalkTiles(&tile, 50, 100, 150, 50) && tile == 1, "walk positions are world units");
}

void TestLocus() {
    Stan stan;
    stan.Load(MakeFloor(100, 0, 10, 3), 1.0f);

    Check(stan.TestCircle(0, 50, 75, 5) == StanHit::None, "a circle inside a tile touches nothing");

    std::vector<int> visited;
    StanLocusCallbacks tracing;
    tracing.onTile = [&](int tile) { visited.push_back(tile); };
    Check(stan.Locus(0, 50, 55, 10, tracing) == StanHit::None && visited == std::vector<int>({ 0, 1 }),
          "a circle on the diagonal follows the link");

    Check(stan.TestCircle(0, 50, 95, 10) == StanHit::Found && stan.LastCollision().tile == 0 &&
              stan.LastCollision().point == 1,
          "a circle at an edge with no neighbour hits it");

    int blockedCalls = 0;
    StanLocusCallbacks carryOn;
    carryOn.onBlocked = [&](const int*, int) {
        blockedCalls++;
        return true;
    };
    Check(stan.Locus(0, 50, 95, 10, carryOn) == StanHit::None && blockedCalls == 2,
          "a blocked callback that returns true lets the search go on, and is called once more at the end");

    StanLocusCallbacks treatAsBlocking;
    treatAsBlocking.onEdge = [](int, int point, float, float, float) { return point == 2; };
    Check(stan.Locus(0, 50, 55, 10, treatAsBlocking) == StanHit::Found, "an edge callback can refuse to follow a link");

    Stan scaled;
    scaled.Load(MakeFloor(100, 0, 10, 3), 0.5f);
    Check(scaled.TestCircle(0, 100, 190, 20) == StanHit::Found && scaled.TestCircle(0, 100, 150, 10) == StanHit::None,
          "the circle is in world units");
}

void TestFindNearestWalkable() {
    auto data = MakeFloor(100, 0, 10, 3);
    // A q tile is skipped even when it is the closest.
    data->tiles.push_back(
        MakeTile(0x80070B, 5, { { { 200, 0, 0 }, { 200, 0, 60 }, { 260, 0, 60 } } }, { kNoTile, kNoTile, kNoTile }));
    Stan stan;
    stan.Load(data, 1.0f);

    Vec3 position = { 95, 0, 5 };
    const int tile = stan.FindNearestWalkable(&position, 0.0f);
    Check(tile == 1 && Near(position[0], 96.6667f, 0.01f) && Near(position[1], 9.6667f, 0.01f) &&
              Near(position[2], 3.3333f, 0.01f),
          "the nearest walkable point is just inside the closest corner");

    Vec3 far = { 230, 0, 30 };
    Check(stan.FindNearestWalkable(&far, 0.0f) == 1, "a q tile is never chosen");

    Vec3 crowded = { 0, 0, 0 };
    const int wide = stan.FindNearestWalkable(&crowded, 15.0f);
    const Vec3 mid = stan.TileMidPoint(wide);
    Check((wide == 0 || wide == 1) && crowded == mid, "a wide circle only fits at a tile's centre");

    Vec3 unchanged = { 1, 2, 3 };
    Check(stan.FindNearestWalkable(&unchanged, 500.0f) == kNoTile && unchanged == Vec3({ 1, 2, 3 }),
          "no room for the circle finds no tile and leaves the position");
}

void TestResolvePadTile() {
    Stan stan;
    stan.Load(MakeFloor(100, 0, 10, 3), 1.0f);

    int tile = kNoTile;
    Check(stan.ResolvePadTile({ 75, 0, 25 }, "p5a2", &tile) == 1 && tile == 1, "a pad on its named tile keeps it");
    Check(stan.ResolvePadTile({ 75, 0, 25 }, "p5a1", &tile) == 2 && tile == 1,
          "a pad off its named tile takes the tile that holds it");
    Check(stan.ResolvePadTile({ 75, 0, 25 }, "nonsense", &tile) == 2 && tile == 1, "an unknown name is searched for");
    Check(stan.ResolvePadTile({ 500, 0, 500 }, "p5a1", &tile) == 0 && tile == kNoTile,
          "a pad no tile reaches has no tile");
}

void TestLineAndVolume() {
    Stan stan;
    stan.Load(MakeFloor(100, 0, 10, 3), 1.0f);

    int tile = 0;
    Check(stan.TestLineUnobstructed(&tile, 25, 50, 75, 25, 0, 0, 0, 0, 1) != 0 && tile == 1,
          "a clear line reaches its end tile");

    tile = 1;
    Check(stan.TestLineUnobstructed(&tile, 50, 25, 150, 25, 0, 0, 0, 0, 1) == 0 && stan.LastCollision().hit &&
              Near(stan.LastCollision().fraction, 0.5f),
          "a line out of the tiles stops at the edge");
    Vec3 a{};
    Vec3 b{};
    Check(stan.CollisionEdge(&a, &b) && Near(a[0], 100.0f) && Near(b[0], 100.0f), "and names that edge");
    stan.ResetHits();
    Check(!stan.CollisionEdge(&a, &b), "resetting clears it");

    tile = 0;
    Check(stan.TestVolume(&tile, 50, 75, 5, 0, 0, 0) < 0 && stan.TestVolume(&tile, 50, 95, 10, 0, 0, 0) == 2,
          "a circle in the open is clear and one at a wall is not");

    Check(stan.TestLocusEdgeAboveY(0, 50, 55, 10, 5.0f) == StanHit::None &&
              stan.TestLocusEdgeAboveY(0, 50, 55, 10, -5.0f) == StanHit::Found,
          "a linked edge is blocking only when both its ends are above the height");

    int rooms[4];
    int count = 0;
    Check(stan.TestCircleRooms(0, 50, 95, 10, rooms, &count, 4) == StanHit::Found && count == 1 && rooms[0] == 3,
          "the room test collects rooms and reports two near edges");
}

void TestSpecialTiles() {
    auto data = MakeFloor(100, 0, 10, 3);
    data->tiles[1].mid = 0x1000;
    Stan kneel;
    kneel.Load(data, 1.0f);
    Check(kneel.TileDistance(0, 50, 55, 10).forceCrouch, "a kneeling tile beside the circle forces a crouch");

    data->tiles[0].mid = 0x3000;
    data->tiles[1].mid = 0x3000;
    Stan ladder;
    ladder.Load(data, 1.0f);
    const StanTileProbe probe = ladder.TileDistance(0, 50, 55, 10);
    Check(probe.ladder && !probe.forceCrouch, "a ladder tile beside the circle is noted");
    int tile1 = kNoTile;
    int tile2 = kNoTile;
    std::array<Vec3, 4> points{};
    Check(ladder.GetLadderCollision(&tile1, &tile2, &points) && tile1 + tile2 == 1 && tile1 != tile2,
          "and its neighbour and points are found");
}

void TestSegmentMath() {
    Check(Near(SegmentIntersectionFraction({ 0, 0 }, { 10, 0 }, { 5, -5 }, { 5, 5 }), 0.5f) &&
              SegmentIntersectionFraction({ 0, 0 }, { 10, 0 }, { 50, -5 }, { 50, 5 }) == 1.0f,
          "the line fraction is where it crosses, or 1");
    Check(Near(CircleSegmentContactFraction(1.0f, { 0, 0 }, { 5, -5 }, { 5, 5 }, { 10, 0 }), 0.4f) &&
              CircleSegmentContactFraction(1.0f, { 0, 0 }, { 50, -5 }, { 50, 5 }, { 10, 0 }) == 1.0f,
          "a circle touches a wall a radius early");
}

} // namespace

int main() {
    TestLineAndVolume();
    TestSpecialTiles();
    TestSegmentMath();
    TestParsePayload();
    TestRoomTables();
    TestMatchTileName();
    TestHeights();
    TestWalk();
    TestLocus();
    TestFindNearestWalkable();
    TestResolvePadTile();
    return gFailures == 0 ? 0 : 1;
}
