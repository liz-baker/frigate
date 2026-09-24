#include "level/Stan.h"

#include <cmath>
#include <cstdlib>

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

// getRotationalDirectionBetween: 1 when b is anticlockwise of a, -1 when
// clockwise (or opposite), 0 for the same direction with |a| >= |b|.
int RotationalDirection(float ax, float az, float bx, float bz) {
    if (az * bx < ax * bz) {
        return 1;
    }
    if (ax * bz < az * bx) {
        return -1;
    }
    if (ax * bx < 0 || az * bz < 0) {
        return -1;
    }
    if (ax * ax + az * az < bx * bx + bz * bz) {
        return 1;
    }
    return 0;
}

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
