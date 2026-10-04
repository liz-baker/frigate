#include "level/Stan.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "level/StanIntersection.h"

namespace Frigate::Level {

namespace {

// Tile links the locus traversal follows before giving up, and the room for
// the tiles one visited tile can still add after that check.
constexpr int kLocusTileLimit = 41;
constexpr int kLocusStackSize = 64;
// The walk gives up after this many tiles.
constexpr int kWalkTileLimit = 0x1F5;

// sub_GAME_7F0AFB78's starting distance, M_U32_MAX_VALUE_F.
constexpr float kFarthest = 4294967296.0f;

// g_StanTileSpecialFlags, indexed by a tile's special kind.
constexpr std::array<uint8_t, 16> kSpecialFlags = { 0x8D, 0x86, 0x04, 0xC5, 0x9D, 0xA4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// sub_GAME_7F0B07BC: whether the segment (s0..s1) crosses the edge (e0..e1),
// judged by `threshold` (1 counts touching as a miss, 0 as a crossing).
bool SegmentCrossesEdge(float s0x, float s0z, float s1x, float s1z, float e0x, float e0z, float e1x, float e1z,
                        int threshold) {
    float bx = -(s0x - e0x);
    float bz = -(s0z - e0z);
    float ax = s1x - s0x;
    float az = s1z - s0z;
    float cx = e1x - s0x;
    float cz = e1z - s0z;
    const int side1 = RotationalDirection(ax, az, bx, bz) * RotationalDirection(ax, az, cx, cz);

    ax = e1x - e0x;
    az = e1z - e0z;
    bx = s0x - e0x;
    bz = s0z - e0z;
    cx = s1x - e0x;
    cz = s1z - e0z;
    const int side2 = RotationalDirection(ax, az, bx, bz) * RotationalDirection(ax, az, cx, cz);

    return side1 < threshold && side2 < threshold;
}

// stanPointProjectsOntoTileEdge.
bool ProjectsOntoEdge(const StanTile& tile, int edge, float x, float z) {
    const StanPoint& from = tile.points[edge];
    const StanPoint& to = tile.points[(edge + 1) % tile.PointCount()];
    const float startX = from.x;
    const float startZ = from.z;
    const float edgeX = static_cast<float>(to.x) - startX;
    const float edgeZ = static_cast<float>(to.z) - startZ;
    x -= startX;
    z -= startZ;
    const float lengthSquared = edgeX * edgeX + edgeZ * edgeZ;
    const float dot = x * edgeX + z * edgeZ;
    return (lengthSquared < dot && dot < 0.0f) || (0.0f < dot && dot < lengthSquared);
}

// distToTilePnt2D.
float DistanceToPoint(const StanTile& tile, int point, float x, float z) {
    x -= static_cast<float>(tile.points[point].x);
    z -= static_cast<float>(tile.points[point].z);
    return std::sqrt(x * x + z * z);
}

// stanPackId: the two id fields a tile name packs into, or false for a name
// the game rejects.
bool PackId(const std::string& id, uint16_t* hi, uint8_t* lo) {
    const char* text = id.c_str();
    const int letter = text[0] - 'p';
    if (letter < 0 || letter > 1) {
        return false;
    }
    char* end = nullptr;
    const uint32_t number = static_cast<uint32_t>(std::strtol(text + 1, &end, 10));
    if (text == end - 1 || number > 32767) {
        return false;
    }
    if (end[0] - 'a' < 0 || end[0] - 'a' >= 26) {
        return false;
    }
    const int file = end[0] - 'a';
    int subTriangle = end[1];
    if (subTriangle != 0 && subTriangle != '0') {
        subTriangle -= '0';
    }
    if (subTriangle < 0 || subTriangle >= 8) {
        return false;
    }
    if (end[1] != 0 && end[2] != 0) {
        return false;
    }
    *hi = static_cast<uint16_t>((letter << 15) | number);
    *lo = static_cast<uint8_t>((file << 3) | subTriangle);
    return true;
}

} // namespace

bool Stan::Load(std::shared_ptr<const StanData> data, float scale) {
    data_ = std::move(data);
    scale_ = scale;
    inverseScale_ = 1.0f / scale;
    saved_ = SavedCollision{};
    ladderTile_ = kNoTile;
    roomCount_ = 0;
    firstInRoom_.fill(kNoTile);
    bounds_.fill(RoomBounds{});

    // stanBuildRoomData
    int lastRoom = 0xFF;
    const int tileCount = TileCount();
    for (int index = 0; index < tileCount; index++) {
        const StanTile& tile = Tile(index);
        if (tile.room >= kMaxRooms) {
            data_.reset();
            return false;
        }
        if (tile.room != lastRoom) {
            lastRoom = tile.room;
            if (roomCount_ <= lastRoom) {
                roomCount_ = lastRoom + 1;
            }
            firstInRoom_[lastRoom] = index;
            bounds_[lastRoom].min = { 0x7FFF, 0x7FFF, 0x7FFF };
            bounds_[lastRoom].max = { -0x8000, -0x8000, -0x8000 };
        }
        for (int i = 0; i < tile.PointCount(); i++) {
            const StanPoint& point = tile.points[i];
            const std::array<int16_t, 3> coordinates = { point.x, point.y, point.z };
            for (int j = 0; j < 3; j++) {
                if (coordinates[j] < bounds_[lastRoom].min[j]) {
                    bounds_[lastRoom].min[j] = coordinates[j];
                }
                if (coordinates[j] > bounds_[lastRoom].max[j]) {
                    bounds_[lastRoom].max[j] = coordinates[j];
                }
            }
        }
    }
    return true;
}

int Stan::FillSearch(int start, const std::function<bool(int tile)>& predicate) const {
    // bfsTileStack holds 352 tiles; the search gives up at 351 visited.
    constexpr int kVisitLimit = 351;
    if (start == kNoTile) {
        return kNoTile;
    }
    if (predicate(start)) {
        return start;
    }
    std::vector<int> seen{ start };
    // Each wave restarts at the beginning of the stack and stops at the count
    // the wave began with.
    for (int waveEnd = 1;; waveEnd = static_cast<int>(seen.size())) {
        for (int index = 0; index < waveEnd; index++) {
            const StanTile& tile = Tile(seen[static_cast<size_t>(index)]);
            for (int point = 0; point < tile.PointCount(); point++) {
                const int linked = tile.points[static_cast<size_t>(point)].link;
                if (linked == kNoTile || std::find(seen.begin(), seen.end(), linked) != seen.end()) {
                    continue;
                }
                if (predicate(linked)) {
                    return linked;
                }
                seen.push_back(linked);
                if (static_cast<int>(seen.size()) >= kVisitLimit) {
                    return kNoTile;
                }
            }
        }
        if (static_cast<int>(seen.size()) == waveEnd) {
            return kNoTile;
        }
    }
}

int Stan::MatchTileName(const std::string& id) const {
    if (id.empty()) {
        return kNoTile;
    }
    uint16_t hi = 0;
    uint8_t lo = 0;
    if (!PackId(id, &hi, &lo)) {
        hi = 0xFFFF;
        lo = 0xFF;
    }
    const int tileCount = TileCount();
    for (int index = 0; index < tileCount; index++) {
        const StanTile& tile = Tile(index);
        if (static_cast<uint16_t>(tile.id >> 8) == hi && static_cast<uint8_t>(tile.id) == lo) {
            return index;
        }
    }
    return kNoTile;
}

bool Stan::TileHasZeroArea(int index) const {
    const StanTile& tile = Tile(index);
    const StanPoint& a = tile.points[tile.Triple(0)];
    const StanPoint& b = tile.points[tile.Triple(1)];
    const StanPoint& c = tile.points[tile.Triple(2)];
    const int32_t abX = b.x - a.x;
    const int32_t abZ = b.z - a.z;
    const int32_t acX = c.x - a.x;
    const int32_t acZ = c.z - a.z;
    // The game keeps the cross product in 32 bits.
    const uint32_t cross = static_cast<uint32_t>(static_cast<int64_t>(abZ) * acX - static_cast<int64_t>(abX) * acZ);
    return cross == 0;
}

Vec3 Stan::TileMidPoint(int index) const {
    const StanTile& tile = Tile(index);
    const StanPoint& a = tile.points[tile.Triple(0)];
    const StanPoint& b = tile.points[tile.Triple(1)];
    const StanPoint& c = tile.points[tile.Triple(2)];
    return {
        (((static_cast<float>(a.x) + static_cast<float>(b.x)) + static_cast<float>(c.x)) / 3.0f) * inverseScale_,
        (((static_cast<float>(a.y) + static_cast<float>(b.y)) + static_cast<float>(c.y)) / 3.0f) * inverseScale_,
        (((static_cast<float>(a.z) + static_cast<float>(b.z)) + static_cast<float>(c.z)) / 3.0f) * inverseScale_,
    };
}

Vec3 Stan::PointJustInside(int index, int k) const {
    const StanTile& tile = Tile(index);
    const StanPoint& point = tile.points[tile.Triple(k)];
    Vec3 out = { static_cast<float>(point.x) * inverseScale_, static_cast<float>(point.y) * inverseScale_,
                 static_cast<float>(point.z) * inverseScale_ };
    const Vec3 mid = TileMidPoint(index);
    // 10% of the way from the point towards the tile's centre.
    for (int axis = 0; axis < 3; axis++) {
        out[axis] = (mid[axis] * 0.1f) + (0.9f * out[axis]);
    }
    return out;
}

float Stan::PositionY(int index, float x, float z) const {
    const StanTile& tile = Tile(index);
    x *= scale_;
    z *= scale_;
    const StanPoint& base = tile.points[tile.Triple(0)];
    const StanPoint& second = tile.points[tile.Triple(1)];
    const StanPoint& third = tile.points[tile.Triple(2)];

    const float a[3] = { static_cast<float>(second.x - base.x), static_cast<float>(second.y - base.y),
                         static_cast<float>(second.z - base.z) };
    const float b[3] = { static_cast<float>(third.x - base.x), static_cast<float>(third.y - base.y),
                         static_cast<float>(third.z - base.z) };

    // The plane's normal, truncated to integers as the game does.
    const int64_t normal[3] = { static_cast<int64_t>((a[1] * b[2]) - (a[2] * b[1])),
                                static_cast<int64_t>((a[2] * b[0]) - (a[0] * b[2])),
                                static_cast<int64_t>((a[0] * b[1]) - (a[1] * b[0])) };
    const int64_t sum = normal[0] * base.x + normal[1] * base.y + normal[2] * base.z;

    if (normal[1] == 0) {
        return static_cast<float>(base.y) * inverseScale_;
    }
    return static_cast<float>(((static_cast<double>(sum) - static_cast<double>(x) * static_cast<double>(normal[0])) -
                               static_cast<double>(z) * static_cast<double>(normal[2])) /
                              static_cast<double>(normal[1])) *
           inverseScale_;
}

float Stan::EdgeDistance(const StanTile& tile, int edge, float x, float z) const {
    const int next = (edge + 1) % tile.PointCount();
    const StanPoint& to = tile.points[next];
    const StanPoint& from = tile.points[edge];
    const float edgeX = static_cast<float>(to.x - from.x);
    const float edgeZ = static_cast<float>(to.z - from.z);
    const float length = std::sqrt(edgeX * edgeX + edgeZ * edgeZ);
    if (length == 0) {
        const float dx = x - static_cast<float>(to.x);
        const float dz = z - static_cast<float>(to.z);
        return std::sqrt(dx * dx + dz * dz);
    }
    const float cross = edgeZ * (x - static_cast<float>(from.x)) + -edgeX * (z - static_cast<float>(from.z));
    return cross / length;
}

float Stan::TripleEdgeDistance(const StanTile& tile, int k, float x, float z) const {
    const int endK = k != 2 ? k + 1 : 0;
    const StanPoint& from = tile.points[tile.Triple(k)];
    const StanPoint& to = tile.points[tile.Triple(endK)];
    const float edgeX = static_cast<float>(to.x - from.x);
    const float edgeZ = static_cast<float>(to.z - from.z);
    const float length = std::sqrt((edgeX * edgeX) + (edgeZ * edgeZ));
    if (length == 0.0f) {
        const float dx = x - static_cast<float>(to.x);
        const float dz = z - static_cast<float>(to.z);
        return std::sqrt((dx * dx) + (dz * dz));
    }
    const float cross = (edgeZ * (x - static_cast<float>(from.x))) + (-edgeX * (z - static_cast<float>(from.z)));
    return cross / length;
}

bool Stan::PointInsideTriangle(int index, float x, float z) const {
    const StanTile& tile = Tile(index);
    for (int k = 0; k != 3; k++) {
        const float disp = TripleEdgeDistance(tile, k, x * scale_, z * scale_) * inverseScale_;
        if (disp < 0) {
            return false;
        }
    }
    return true;
}

bool Stan::WalkTiles(int* tileStack, float startX, float startZ, float destX, float destZ) {
    return WalkTilesImpl(tileStack, startX, startZ, destX, destZ, nullptr);
}

bool Stan::WalkTilesImpl(int* tileStack, float startX, float startZ, float destX, float destZ,
                         const std::function<void(int, int)>& callback) {
    startX *= scale_;
    startZ *= scale_;
    destX *= scale_;
    destZ *= scale_;

    int tile = *tileStack;
    int previousTile = *tileStack;
    int previousPreviousTile = *tileStack;
    const float lineNegDz = -(destZ - startZ);
    const float lineDx = destX - startX;
    int nextTile = kNoTile;
    int iterationCount = 0;
    int savedPointIndex = -1;

    while (true) {
        int crossings = 0;
        if (callback) {
            callback(tile, previousTile);
        }
        const StanTile& current = Tile(tile);
        const int count = current.PointCount();

        for (int edge = 0; edge < count; edge++) {
            const StanPoint& from = current.points[edge];
            const StanPoint& to = current.points[(edge + 1) % count];

            // Only edges the segment leaves the tile through.
            if (((lineNegDz * (to.x - from.x)) + (lineDx * (to.z - from.z))) <= 0.0f) {
                const bool hasLink = from.link != kNoTile;
                if (SegmentCrossesEdge(startX, startZ, destX, destZ, from.x, from.z, to.x, to.z, hasLink ? 1 : 0)) {
                    crossings++;
                    if (previousTile != from.link && previousPreviousTile != from.link) {
                        savedPointIndex = edge;
                        nextTile = from.link;
                    }
                }
            }
        }

        previousPreviousTile = previousTile;
        previousTile = tile;

        if (tile == nextTile) {
            crossings = 0;
        }
        tile = nextTile;

        if (crossings == 0) {
            return true;
        }
        if (iterationCount++ >= kWalkTileLimit || nextTile == kNoTile) {
            saved_.tile = previousTile;
            saved_.point = savedPointIndex;
            return false;
        }
        *tileStack = nextTile;
    }
}

StanHit Stan::Locus(int startTile, float x, float z, float radius, const StanLocusCallbacks& callbacks) {
    x *= scale_;
    z *= scale_;
    radius *= scale_;

    int visited = 0;
    int stackCount = 1;
    std::array<int, kLocusStackSize> stack{};
    stack[0] = startTile;

    do {
        const int tileIndex = stack[visited++];
        const StanTile& tile = Tile(tileIndex);
        const int count = tile.PointCount();

        if (callbacks.onTile) {
            callbacks.onTile(tileIndex);
        }

        int point = 0;
        if (count > 0) {
            int pointNext;
            do {
                pointNext = point + 1;
                const int nextPoint = pointNext % count;
                const float edgeDistance = EdgeDistance(tile, point, x, z);
                const float distanceA = DistanceToPoint(tile, point, x, z);
                const float distanceB = DistanceToPoint(tile, nextPoint, x, z);

                if (edgeDistance < radius &&
                    (distanceA < radius || distanceB < radius || ProjectsOntoEdge(tile, point, x, z))) {
                    const bool blocked =
                        callbacks.onEdge && callbacks.onEdge(tileIndex, point, edgeDistance, distanceA, distanceB);
                    const int linked = tile.points[point].link;
                    if (!blocked && linked != kNoTile) {
                        bool seen = false;
                        for (int i = stackCount - 1; i >= 0; i--) {
                            if (linked == stack[i]) {
                                seen = true;
                                break;
                            }
                        }
                        if (!seen) {
                            stack[stackCount++] = linked;
                        }
                    } else {
                        saved_.tile = tileIndex;
                        saved_.point = point;
                        if (!(callbacks.onBlocked && callbacks.onBlocked(stack.data(), stackCount))) {
                            return StanHit::Found;
                        }
                    }
                }
                point = pointNext;
            } while (pointNext < count);
        }

        if (stackCount >= kLocusTileLimit) {
            return StanHit::TraversalLimit;
        }
    } while (visited < stackCount);

    if (callbacks.onBlocked) {
        callbacks.onBlocked(stack.data(), stackCount);
    }
    return StanHit::None;
}

int Stan::FindNearestWalkable(Vec3* position, float radius) {
    int bestTile = kNoTile;
    const Vec3 original = *position;
    float bestDistance = kFarthest;
    constexpr int kMidPointIndex = 3;

    const int tileCount = TileCount();
    for (int index = 0; index < tileCount; index++) {
        // The type bit of the tile's name; only tiles with it clear are used.
        if (((Tile(index).id >> 23) & 1) == 1 || TileHasZeroArea(index)) {
            continue;
        }
        for (int i = 0; i != 4; i++) {
            const Vec3 candidate = i == kMidPointIndex ? TileMidPoint(index) : PointJustInside(index, i);
            if (TestCircle(index, candidate[0], candidate[2], radius) == StanHit::None) {
                const float dx = original[0] - candidate[0];
                const float dy = original[1] - candidate[1];
                const float dz = original[2] - candidate[2];
                const float distance = dx * dx + dy * dy + dz * dz;
                if (distance < bestDistance) {
                    bestTile = index;
                    bestDistance = distance;
                    *position = candidate;
                }
            }
        }
    }
    return bestTile;
}

bool Stan::PointWithinTileBounds(int index, float x, float z) const {
    const StanTile& tile = Tile(index);
    x *= scale_;
    z *= scale_;
    for (int k = 0; k != 3; k++) {
        // sub_GAME_7F0B0400
        const StanPoint& from = tile.points[tile.Triple(k)];
        const StanPoint& to = tile.points[tile.Triple(k != 2 ? k + 1 : 0)];
        const float edgeX = static_cast<float>(to.x - from.x);
        const float edgeZ = static_cast<float>(to.z - from.z);
        const float length = std::sqrt((edgeX * edgeX) + (edgeZ * edgeZ));
        const float distance =
            length == 0.0f
                ? 0.0f
                : ((edgeZ * (x - static_cast<float>(from.x))) + ((z - static_cast<float>(from.z)) * -edgeX)) / length;
        if (distance < -2) {
            return false;
        }
    }
    return true;
}

bool Stan::WalkTilesNotingRooms(int* tile, float startX, float startZ, float destX, float destZ, int* rooms,
                                int* roomCount, int maxRooms) {
    int lastRoom = -1;
    *roomCount = 0;
    return WalkTilesImpl(tile, startX, startZ, destX, destZ, [&](int current, int) {
        const int room = Tile(current).room;
        if (room != lastRoom && *roomCount < maxRooms) {
            rooms[(*roomCount)++] = room;
            lastRoom = room;
        }
    });
}

int Stan::RoomsBetween(int startTile, float startX, float startZ, int* destTile, float destX, float destZ, int* rooms,
                       int maxRooms, const std::function<bool(int, int)>& sharePortal) {
    if (*destTile != kNoTile) {
        const int roomA = Tile(startTile).room;
        const int roomB = Tile(*destTile).room;
        if (roomB == roomA) {
            rooms[0] = roomA;
            return 1;
        }
        if (sharePortal && sharePortal(roomA & 0xFF, roomB & 0xFF)) {
            rooms[0] = roomA;
            rooms[1] = roomB;
            return 2;
        }
    }

    int reached = startTile;
    int count = 0;
    if (!WalkTilesNotingRooms(&reached, startX, startZ, destX, destZ, rooms, &count, maxRooms)) {
        return 0;
    }
    if (maxRooms < count) {
        count = maxRooms;
    }
    if (*destTile == kNoTile) {
        *destTile = reached;
    }
    if (reached != *destTile) {
        return 0;
    }
    return count;
}

int Stan::FindPropOnLine(int tile, float x, float z, float destX, float destZ, int cdTypes) {
    int best = -1;
    float bestFraction = 1.0f;
    std::array<int, 21> rooms{};
    int roomCount = 0;
    int reached = tile;

    WalkTilesNotingRooms(&reached, x, z, destX, destZ, rooms.data(), &roomCount, 20);
    if (roomCount >= 21) {
        roomCount = 20;
    }

    if (cdTypes != 0 && props_ != nullptr) {
        rooms[static_cast<size_t>(roomCount)] = -1;
        std::vector<PropCollider> colliders;
        props_->Colliders(rooms.data(), cdTypes, &colliders);
        for (const PropCollider& collider : colliders) {
            for (int i = 0; i < collider.edges; i++) {
                const Vec2& from = collider.polygon[static_cast<size_t>(i)];
                const Vec2& to = collider.polygon[static_cast<size_t>((i + 1) % collider.edges)];
                if (!SegmentsIntersect({ x, z }, { destX, destZ }, from, to)) {
                    continue;
                }
                const float fraction = SegmentIntersectionFraction({ x, z }, { destX, destZ }, from, to);
                if (fraction < bestFraction) {
                    bestFraction = fraction;
                    best = collider.prop;
                }
            }
        }
    }
    return best;
}

StanHit Stan::TestCircleRooms(int tile, float x, float z, float radius, int* rooms, int* roomCount, int maxRooms) {
    int count = 0;
    int nearEdges = 0;
    StanLocusCallbacks callbacks;
    // stanLocusAddTileRoomIfNew
    callbacks.onTile = [&](int visited) {
        const int room = Tile(visited).room;
        for (int i = 0; i < count; i++) {
            if (rooms[i] == room) {
                return;
            }
        }
        if (count < maxRooms) {
            rooms[count++] = room;
        }
    };
    // incrNearEdgeCount
    callbacks.onBlocked = [&](const int*, int) {
        nearEdges++;
        return true;
    };
    const StanHit result = Locus(tile, x, z, radius, callbacks);
    *roomCount = count;
    return nearEdges > 1 ? StanHit::Found : result;
}

StanTileProbe Stan::TileDistance(int tile, float x, float z, float radius) {
    StanTileProbe probe;
    StanLocusCallbacks callbacks;
    // stanIsSpecialBit1Set
    callbacks.onTile = [&](int visited) {
        if (kSpecialFlags[Tile(visited).Special()] & kStanTileForceCrouch) {
            probe.forceCrouch = true;
        }
    };
    // stanCheckLinkedSpecialTile
    callbacks.onEdge = [&](int visited, int point, float, float, float) {
        const int link = Tile(visited).points[point].link;
        if (link != kNoTile) {
            const uint8_t flags = kSpecialFlags[Tile(link).Special()];
            if (flags & kStanTileForceCrouch) {
                probe.forceCrouch = true;
                return true;
            }
            if (flags & kStanTileLadder) {
                ladderTile_ = link;
                probe.ladder = true;
                return false;
            }
        }
        return false;
    };
    probe.hit = Locus(tile, x, z, radius, callbacks);
    return probe;
}

StanHit Stan::TestLocusEdgeAboveY(int tile, float x, float z, float radius, float y) {
    const float threshold = y * scale_;
    StanLocusCallbacks callbacks;
    // stanLocusEdgeIsAboveY
    callbacks.onEdge = [&](int visited, int edge, float, float, float) {
        const StanTile& t = Tile(visited);
        if (threshold < static_cast<float>(t.points[edge].y)) {
            const int next = (edge + 1) % t.PointCount();
            if (threshold < static_cast<float>(t.points[next].y)) {
                return true;
            }
        }
        return false;
    };
    return Locus(tile, x, z, radius, callbacks);
}

int Stan::TestLineUnobstructed(int* tile, float x, float z, float destX, float destZ, int cdTypes, float height,
                               float above, float low, float high) {
    float fraction = 1.0f;
    int reached = *tile;
    std::array<int, 21> rooms{};
    int roomCount = 0;
    const bool heightTested = above <= height;
    bool heightsSet = false;
    float slopeHigh = 0;
    float slopeLow = 0;

    const int result = WalkTilesNotingRooms(&reached, x, z, destX, destZ, rooms.data(), &roomCount, 20);
    int retval = result;
    if (result == 0) {
        const StanTile& blocked = Tile(saved_.tile);
        const int next = (saved_.point + 1) % blocked.PointCount();
        saved_.hit = true;
        saved_.pointA = { static_cast<float>(blocked.points[saved_.point].x) * inverseScale_,
                          static_cast<float>(blocked.points[saved_.point].z) * inverseScale_ };
        saved_.pointB = { static_cast<float>(blocked.points[next].x) * inverseScale_,
                          static_cast<float>(blocked.points[next].z) * inverseScale_ };
        fraction = SegmentIntersectionFraction({ x, z }, { destX, destZ }, saved_.pointA, saved_.pointB);
    }

    saved_.prop = -1;
    // The fractions are measured along the line as asked for, even after the
    // end used for the tests is cut short.
    const Vec2 fullDest = { destX, destZ };
    if (cdTypes != 0 && props_ != nullptr) {
        rooms[static_cast<size_t>(roomCount)] = -1;
        std::vector<PropCollider> colliders;
        props_->Colliders(rooms.data(), cdTypes, &colliders);
        for (const PropCollider& collider : colliders) {
            for (int i = 0; i < collider.edges; i++) {
                const Vec2& from = collider.polygon[static_cast<size_t>(i)];
                const Vec2& to = collider.polygon[static_cast<size_t>((i + 1) % collider.edges)];
                if (!SegmentsIntersect({ x, z }, { destX, destZ }, from, to)) {
                    continue;
                }
                const float contact = SegmentIntersectionFraction({ x, z }, fullDest, from, to);
                if (contact >= fraction) {
                    continue;
                }
                bool blocks = true;
                if (heightTested) {
                    if (!heightsSet) {
                        heightsSet = true;
                        if (high <= low) {
                            slopeLow = low - height;
                            slopeHigh = high - above;
                        } else {
                            if (fraction < 1.0f) {
                                destX = x + (destX - x) * fraction;
                                destZ = z + (destZ - z) * fraction;
                            }
                            // The line's own rise, from the start tile to where it is cut short.
                            const float startY = PositionY(*tile, x, z);
                            height += startY;
                            above += startY;
                            const float slope = (PositionY(reached, destX, destZ) - startY) / fraction;
                            slopeHigh = slope;
                            slopeLow = slope;
                        }
                    }
                    if (collider.top <= slopeHigh * contact + above || slopeLow * contact + height <= collider.bottom) {
                        blocks = false;
                    }
                }
                if (blocks) {
                    retval = 0;
                    fraction = contact;
                    saved_.hit = true;
                    saved_.pointA = from;
                    saved_.pointB = to;
                    saved_.tile = kNoTile;
                    saved_.point = 0;
                    saved_.prop = collider.prop;
                    reached = kNoTile;
                }
            }
        }
    }

    if (reached == kNoTile) {
        // A prop cut the line short: the tiles are walked again to where it ends.
        reached = *tile;
        destX = x + (destX - x) * fraction;
        destZ = z + (destZ - z) * fraction;
        WalkTiles(&reached, x, z, destX, destZ);
    }

    *tile = reached;
    saved_.fraction = fraction;
    return retval;
}

int Stan::TestVolume(int* tile, float x, float z, float radius, int cdTypes, float height, float above) {
    std::array<int, 21> rooms{};
    int roomCount = 0;
    const StanHit hit = TestCircleRooms(*tile, x, z, radius, rooms.data(), &roomCount, 20);
    if (static_cast<int32_t>(hit) >= 0) {
        return static_cast<int32_t>(hit);
    }
    saved_.prop = -1;
    if (cdTypes == 0 || props_ == nullptr) {
        return static_cast<int32_t>(StanHit::None);
    }
    const bool heightTested = above <= height;
    if (heightTested) {
        const float floorY = PositionY(*tile, x, z);
        height += floorY;
        above += floorY;
    }
    rooms[static_cast<size_t>(roomCount)] = -1;
    std::vector<PropCollider> colliders;
    props_->Colliders(rooms.data(), cdTypes, &colliders);
    const Vec2 point = { x, z };
    for (const PropCollider& collider : colliders) {
        if (heightTested && !(collider.bottom <= height && above <= collider.top)) {
            continue;
        }
        float nearest = -1.0f;
        for (int i = 0; i < collider.edges; i++) {
            const Vec2& from = collider.polygon[static_cast<size_t>(i)];
            const Vec2& to = collider.polygon[static_cast<size_t>((i + 1) % collider.edges)];
            const float distance = std::fabs(SignedPointLineDistance(from, to, point));
            if (nearest < distance && distance < radius &&
                (Distance2d(from, point) < radius || Distance2d(to, point) < radius ||
                 PointProjectsOntoEdge(from, to, point))) {
                nearest = distance;
                saved_.hit = true;
                saved_.pointA = from;
                saved_.pointB = to;
                saved_.tile = kNoTile;
                saved_.point = 0;
                saved_.prop = collider.prop;
            }
        }
        if (nearest > -1.0f) {
            return static_cast<int32_t>(StanHit::Found);
        }
    }
    return static_cast<int32_t>(StanHit::None);
}

int Stan::FindTileBelow(const Vec3& position, const uint8_t* rooms, float* y) {
    constexpr float kMaxY = 32767.0f;
    constexpr float kEdgeTolerance = 2.0f;
    float bestY = -3.4028235e38f;
    int bestTile = kNoTile;
    // The game leaves this set once any tile has an edge that close; later
    // tiles are then always walked to.
    bool nearEdge = false;

    const float scaledX = position[0] * scale_;
    float scaledY = position[1] * scale_;
    const float scaledZ = position[2] * scale_;
    scaledY = std::min(scaledY, kMaxY);
    scaledY = std::max(scaledY, -kMaxY);
    const int16_t shortX = static_cast<int16_t>(scaledX);
    const int16_t shortY = static_cast<int16_t>(scaledY);
    const int16_t shortZ = static_cast<int16_t>(scaledZ);

    for (int room = 0; room < roomCount_; room++) {
        const int first = firstInRoom_[static_cast<size_t>(room)];
        if (first == kNoTile) {
            continue;
        }
        const RoomBounds& bounds = bounds_[static_cast<size_t>(room)];
        if (shortX < bounds.min[0] || bounds.max[0] < shortX || shortZ < bounds.min[2] || bounds.max[2] < shortZ ||
            shortY < bounds.min[1]) {
            continue;
        }
        if (rooms != nullptr) {
            bool listed = false;
            for (int i = 0; rooms[i] != 0xFF && i != 4; i++) {
                if (room == rooms[i]) {
                    listed = true;
                    break;
                }
            }
            if (!listed) {
                continue;
            }
        }
        for (int tile = first; tile < TileCount() && Tile(tile).room == room; tile++) {
            bool outside = false;
            for (int k = 0; k < 3; k++) {
                const float edgeDistance = TripleEdgeDistance(Tile(tile), k, scaledX, scaledZ);
                if (edgeDistance < -kEdgeTolerance) {
                    outside = true;
                    break;
                }
                if (edgeDistance < kEdgeTolerance) {
                    nearEdge = true;
                }
            }
            if (outside || TileHasZeroArea(tile)) {
                continue;
            }
            if (nearEdge) {
                const Vec3 mid = TileMidPoint(tile);
                int walked = tile;
                if (!WalkTiles(&walked, mid[0], mid[2], position[0], position[2]) || walked != tile) {
                    continue;
                }
            }
            const float tileY = PositionY(tile, position[0], position[2]);
            if (position[1] < tileY) {
                continue;
            }
            if (bestY < tileY) {
                bestTile = tile;
                bestY = tileY;
            }
        }
    }
    if (bestTile != kNoTile && y != nullptr) {
        *y = bestY;
    }
    return bestTile;
}

Vec3 Stan::OrderedPointWorld(int tile, int k) const {
    const StanTile& t = Tile(tile);
    const StanPoint& point = t.points[t.Triple(k)];
    return { point.x * inverseScale_, point.y * inverseScale_, point.z * inverseScale_ };
}

void Stan::EdgePoints(int tile, int point, Vec3* from, Vec3* to) const {
    const StanTile& t = Tile(tile);
    const StanPoint& start = t.points[point];
    const StanPoint& end = t.points[(point + 1) % t.PointCount()];
    *from = { start.x * inverseScale_, start.y * inverseScale_, start.z * inverseScale_ };
    *to = { end.x * inverseScale_, end.y * inverseScale_, end.z * inverseScale_ };
}

Vec3 Stan::CollisionEdgeRayPoint(const Vec3& origin, const Vec3& direction) const {
    Vec3 a{};
    Vec3 b{};
    CollisionEdge(&a, &b);
    const float along =
        ((direction[0] * (a[2] - origin[2])) - (direction[2] * (a[0] - origin[0]))) /
        ((direction[2] * (b[0] - a[0])) - (direction[0] * (b[2] - a[2])));
    return { a[0] + ((b[0] - a[0]) * along), a[1] + ((b[1] - a[1]) * along), a[2] + ((b[2] - a[2]) * along) };
}

bool Stan::CollisionEdge(Vec3* a, Vec3* b) const {
    if (saved_.tile != kNoTile) {
        EdgePoints(saved_.tile, saved_.point, a, b);
        return true;
    }
    if (saved_.hit) {
        *a = { saved_.pointA[0], 0.0f, saved_.pointA[1] };
        *b = { saved_.pointB[0], 0.0f, saved_.pointB[1] };
        return true;
    }
    return false;
}

bool Stan::GetLadderCollision(int* tile1, int* tile2, std::array<Vec3, 4>* points) const {
    const int base = ladderTile_;
    const StanTile& baseTile = Tile(base);
    for (int i = 0; i < baseTile.PointCount(); i++) {
        const int link = baseTile.points[i].link;
        if (link == kNoTile || !(kSpecialFlags[Tile(link).Special()] & kStanTileLadder)) {
            continue;
        }
        const int basePoint = (i + 2) % 3;
        *tile1 = base;
        *tile2 = link;

        // Rotate which of the ladder tile's points go first until the last two
        // points are not below the first two.
        int rotation = 0;
        while (true) {
            for (int k = 0; k < 3; k++) {
                (*points)[(rotation + k) & 3] = OrderedPointWorld(link, ((rotation >> 2) + k) % 3);
            }
            (*points)[(rotation + 3) & 3] = OrderedPointWorld(base, basePoint);

            rotation++;
            if (rotation == 12) {
                break;
            }
            const auto& p = *points;
            if (!(p[2][1] < p[0][1]) && !(p[2][1] < p[1][1]) && !(p[3][1] < p[0][1]) && !(p[3][1] < p[1][1])) {
                break;
            }
        }
        return true;
    }
    return false;
}

int Stan::ResolvePadTile(const Vec3& position, const std::string& tileName, int* tile) {
    *tile = MatchTileName(tileName);
    if (*tile == kNoTile || !PointInsideTriangle(*tile, position[0], position[2])) {
        Vec3 candidate = position;
        *tile = FindNearestWalkable(&candidate, 0.0f);
        if (*tile != kNoTile && WalkTiles(tile, candidate[0], candidate[2], position[0], position[2])) {
            return 2;
        }
        *tile = kNoTile;
        return 0;
    }
    return 1;
}

} // namespace Frigate::Level
