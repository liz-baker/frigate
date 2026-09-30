#include "level/BgLevel.h"

#include <algorithm>
#include <cfloat>
#include <utility>

namespace Frigate::Level {

namespace {

// Rooms with no vertices of their own take their bounds from the stan file.
// Room vertices are room-local, so the bounds scan starts from these.
constexpr int kBoundsStart = 0x7FFF;

// The portals a stage's special cases mark, as inclusive index ranges keyed by
// the stage's row in the level table.
struct SpecialPortals {
    size_t levelIndex;
    std::vector<std::pair<uint8_t, uint8_t>> ranges;
};

const std::vector<SpecialPortals>& SpecialPortalTable() {
    static const std::vector<SpecialPortals> table = {
        { 0x03,
          { { 0x2C, 0x2E },
            { 0x32, 0x37 },
            { 0x3E, 0x3F },
            { 0x4E, 0x56 },
            { 0x59, 0x5D },
            { 0x72, 0x76 },
            { 0x79, 0x7A } } },
        { 0x11, { { 0x00, 0x3A } } },
    };
    return table;
}

Vec3 RoomCenter(const RoomInfo& room) {
    Vec3 center;
    for (int i = 0; i < 3; i++) {
        center[i] = (room.minBounds[i] + room.maxBounds[i]) * 0.5f;
    }
    return center;
}

} // namespace

BgLevel::BgLevel(BgSource& source) : source_(source) {
}

bool BgLevel::Load(LevelId id) {
    UnloadRooms();
    loaded_ = false;
    header_ = {};
    rooms_.assign(kMaxRoomCount, RoomInfo{});
    portalNormalHorizontal_.clear();

    levelIndex_ = FindLevelIndex(id);
    level_ = &LevelEntryAt(levelIndex_);

    if (observer_ != nullptr) {
        observer_->StageLoading();
    }

    error_.clear();
    auto bytes = source_.LoadHeader(*level_);
    if (!bytes) {
        error_ = std::string("no bg header for '") + level_->bgName + "'";
        return false;
    }
    auto header = ParseBgHeader(std::move(*bytes));
    if (!header) {
        error_ = std::string("malformed bg header for '") + level_->bgName + "'";
        return false;
    }
    header_ = std::move(*header);

    // stanLoadFile, and setLevelScale with the stage's scale.
    auto stanData = source_.LoadStan(*level_);
    if (!stanData) {
        error_ = std::string("no stan file for '") + level_->bgName + "'";
        return false;
    }
    if (!stan_.Load(std::move(stanData), level_->scale)) {
        error_ = std::string("malformed stan file for '") + level_->bgName + "'";
        return false;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/122): the stage scale reaches the game's own scale globals
    // (setDebugCameraScale, chrRemoved7F022E1C, and the two conversion
    // factors sub_GAME_7F0B4810 derives); they are read from Level() until
    // those systems exist.
    visibilityScale_ = level_->visibility;
    // TODO(https://github.com/liz-baker/frigate/issues/122): sub_GAME_7F08976C and matrix_4x4_7F058C4C, which pass
    // the stage's visibility scale to their own globals.

    if (header_.firstWord != 0) {
        // No rooms to set up, as in the game; the stage still counts as read.
        loaded_ = true;
        roomLoadBudget_ = 200;
        return true;
    }

    portalNormalHorizontal_.assign(header_.portals.size(), false);

    // initializeRoomData() (bgroomtrans.c) is BgRender::Reset(), which the
    // stage load calls once this returns.

    for (int room = 1; room < header_.maxNumRooms; room++) {
        CalcRoomBounds(room);
    }
    for (size_t portal = 0; portal < header_.portals.size(); portal++) {
        // Points 0..2 span the portal's plane.
        if (header_.portals[portal].points.size() < 3) {
            error_ = "portal " + std::to_string(portal) + " has fewer than three points";
            return false;
        }
        PortalMetric metric = MeasurePortal(header_.portals[portal]);
        portalNormalHorizontal_[portal] =
            metric.normal[0] * metric.normal[0] + metric.normal[2] * metric.normal[2] >= 0.999f;
    }
    for (size_t portal = 0; portal < header_.portals.size(); portal++) {
        OrderPortal(portal);
    }
    for (int room = 0; room < header_.maxNumRooms; room++) {
        ExtendRoomBoundsByPortals(room);
    }
    for (BgPortal& portal : header_.portals) {
        portal.controlBytes1 &= ~kPortalDisabled;
    }
    MarkSpecialPortals();

    // fogRemoved7F0BAA5C(levelid) is an empty function.
    roomLoadBudget_ = 200;
    loaded_ = true;
    return true;
}

int BgLevel::FindWindowPortal(const Vec3& from, const Vec3& to) const {
    int best = -1;
    float bestDistance = FLT_MAX;
    for (size_t i = 0; i < header_.portals.size(); i++) {
        float middle = 0;
        if (PortalCrossing(i, from, to, &middle) != 0) {
            const float distance = middle < 0 ? -middle : middle;
            if (distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
    }
    return best;
}

void BgLevel::AddRoomsThroughPortals(const Vec3& boxMin, const Vec3& boxMax, int* rooms, int* count,
                                     int maxCount) const {
    const float scale = level_->scale;
    Vec3 scaledMin{};
    Vec3 scaledMax{};
    for (int axis = 0; axis < 3; axis++) {
        scaledMin[axis] = boxMin[axis] * scale;
        scaledMax[axis] = boxMax[axis] * scale;
    }

    // The rooms are visited in list order, and rooms added on the way are
    // visited after the ones already there.
    int visited = 0;
    int pass = *count;
    while (true) {
        for (; visited < pass; visited++) {
            const int room = rooms[visited];
            for (const BgPortal& portal : header_.portals) {
                if ((portal.controlBytes1 & kPortalDisabled) != 0 || (room != portal.room1 && room != portal.room2)) {
                    continue;
                }
                Vec3 portalMin = { FLT_MAX, FLT_MAX, FLT_MAX };
                Vec3 portalMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
                for (const Vec3& point : portal.points) {
                    for (int axis = 0; axis < 3; axis++) {
                        portalMin[axis] = std::min(portalMin[axis], point[axis]);
                        portalMax[axis] = std::max(portalMax[axis], point[axis]);
                    }
                }
                // bgIsBboxOverlapping.
                bool overlaps = true;
                for (int axis = 0; axis < 3; axis++) {
                    if (scaledMin[axis] > portalMax[axis] || scaledMax[axis] < portalMin[axis]) {
                        overlaps = false;
                    }
                }
                if (!overlaps) {
                    continue;
                }
                const int other = room == portal.room1 ? portal.room2 : portal.room1;
                if (std::find(rooms, rooms + *count, other) != rooms + *count) {
                    continue;
                }
                if (*count < maxCount) {
                    rooms[(*count)++] = other;
                }
                if (*count >= maxCount) {
                    return;
                }
            }
        }
        if (*count == pass) {
            return;
        }
        pass = *count;
    }
}

int BgLevel::PortalCrossing(size_t portal, const Vec3& from, const Vec3& to, float* midDistance) const {
    const BgPortal& entry = header_.portals[portal];
    const PortalMetric metric = MeasurePortal(entry);
    const float scale = level_->scale;

    const Vec3 diff = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    const float value1 = (from[0] * metric.normal[0] + from[1] * metric.normal[1] + from[2] * metric.normal[2]) * scale;
    const float value2 = (to[0] * metric.normal[0] + to[1] * metric.normal[1] + to[2] * metric.normal[2]) * scale;

    // Both ends compare against min, not max, as the game does.
    if ((value1 < metric.min && value2 < metric.min) || (value1 > metric.min && value2 > metric.min)) {
        return 0;
    }
    if (midDistance != nullptr) {
        *midDistance = (value1 + value2) * 0.5f - metric.min;
    }

    // The segment passes through the opening when it stays on one side of
    // every edge of the portal's polygon.
    bool seenA = false;
    bool seenB = false;
    const size_t count = entry.points.size();
    for (size_t i = 0; i < count; i++) {
        const Vec3& point = entry.points[i];
        const Vec3& next = entry.points[(i + 1) % count];
        const Vec3 edge = { next[0] - point[0], next[1] - point[1], next[2] - point[2] };
        const Vec3 cross = { edge[1] * diff[2] - edge[2] * diff[1], edge[2] * diff[0] - edge[0] * diff[2],
                             edge[0] * diff[1] - edge[1] * diff[0] };
        if (cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2] == 0.0f) {
            return 0;
        }
        const float planeD = cross[0] * point[0] + cross[1] * point[1] + cross[2] * point[2];
        const float side = (cross[0] * from[0] + cross[1] * from[1] + cross[2] * from[2]) * scale;
        if (side < planeD) {
            if (seenB) {
                return 0;
            }
            seenA = true;
        } else {
            if (seenA) {
                return 0;
            }
            seenB = true;
        }
    }
    return value1 < metric.min ? 1 : 2;
}

void BgLevel::Cleanup() {
    UnloadRooms();
    // matrix_4x4_7F058C4C(1.0)
    visibilityScale_ = 1.0f;
}

bool BgLevel::CheckIfRoomModelNeedsLoad(int room) {
    if (room < 0 || room >= kMaxRoomCount) {
        return false;
    }
    rooms_[room].requested = true;
    if (rooms_[room].modelBinLoaded == 0) {
        LoadRoomModelData(room);
        return true;
    }
    return false;
}

void BgLevel::LoadRoomModelData(int room) {
    if (room < 0 || room >= header_.maxNumRooms || rooms_[room].modelBinLoaded != 0) {
        return;
    }

    // The game reads the room's compressed blobs into one block and inflates
    // them there; the packer has already done that, so a room arrives as
    // finished vertices and lists.
    auto geometry = source_.LoadRoom(*level_, room, header_.rooms[room]);
    if (!geometry) {
        error_ = "room " + std::to_string(room) + " of '" + level_->bgName + "' failed to load";
        return;
    }
    rooms_[room].geometry = std::move(*geometry);
    rooms_[room].modelBinLoaded = 1;

    // redarken_lights_in_room, the fixture table's entries and bgApplyDynamicCCRMLUT.
    if (observer_ != nullptr) {
        observer_->RoomLoaded(room, rooms_[room].geometry);
    }
    // TODO(https://github.com/liz-baker/frigate/issues/107): bgBuildRoomVtxBounds(room), the per-batch bounds the
    // ray and bullet tests read.
}

void BgLevel::DeleteRoomData(int room) {
    // TODO(https://github.com/liz-baker/frigate/issues/107): free the room's per-batch vertex bounds along with it.
    rooms_[room].geometry = {};
    rooms_[room].modelBinLoaded = 0;
}

void BgLevel::UnloadRooms() {
    for (int room = 1; room < header_.maxNumRooms; room++) {
        if (rooms_[room].modelBinLoaded != 0) {
            DeleteRoomData(room);
        }
    }
}

void BgLevel::TickUnload() {
    for (int room = 1; room < header_.maxNumRooms; room++) {
        RoomInfo& info = rooms_[room];
        if (info.requested) {
            continue;
        }
        if (info.modelBinLoaded == 4) {
            DeleteRoomData(room);
        } else if (info.modelBinLoaded != 0) {
            info.modelBinLoaded++;
        }
    }
}

void BgLevel::CalcRoomBounds(int room) {
    // A room with no vertices of its own takes its bounds and position from
    // the stan file's room bounds.
    if (header_.rooms[room].vertexBlob == 0) {
        if (room < stan_.RoomCount() && stan_.FirstTileInRoom(room) != kNoTile) {
            const Stan::RoomBounds& bounds = stan_.Bounds(room);
            for (int axis = 0; axis < 3; axis++) {
                rooms_[room].minBounds[axis] = bounds.min[axis];
                rooms_[room].maxBounds[axis] = bounds.max[axis];
                header_.rooms[room].position[axis] = static_cast<float>((bounds.min[axis] + bounds.max[axis]) / 2);
            }
        }
        return;
    }

    bool wasLoaded = rooms_[room].modelBinLoaded != 0;
    if (!wasLoaded) {
        LoadRoomModelData(room);
    }

    int32_t minimum[3] = { kBoundsStart, kBoundsStart, kBoundsStart };
    int32_t maximum[3] = { -kBoundsStart, -kBoundsStart, -kBoundsStart };
    for (const auto& vertex : rooms_[room].geometry.vertexPositions) {
        for (int axis = 0; axis < 3; axis++) {
            minimum[axis] = vertex[axis] < minimum[axis] ? vertex[axis] : minimum[axis];
            maximum[axis] = vertex[axis] > maximum[axis] ? vertex[axis] : maximum[axis];
        }
    }
    for (int axis = 0; axis < 3; axis++) {
        rooms_[room].minBounds[axis] = header_.rooms[room].position[axis] + static_cast<float>(minimum[axis]);
        rooms_[room].maxBounds[axis] = header_.rooms[room].position[axis] + static_cast<float>(maximum[axis]);
    }

    if (!wasLoaded) {
        DeleteRoomData(room);
    }
}

void BgLevel::ExtendRoomBoundsByPortals(int room) {
    for (const BgPortal& portal : header_.portals) {
        if (portal.room1 != room && portal.room2 != room) {
            continue;
        }
        for (const Vec3& point : portal.points) {
            for (int axis = 0; axis < 3; axis++) {
                if (point[axis] < rooms_[room].minBounds[axis]) {
                    rooms_[room].minBounds[axis] = point[axis];
                }
                if (rooms_[room].maxBounds[axis] < point[axis]) {
                    rooms_[room].maxBounds[axis] = point[axis];
                }
            }
        }
    }
}

// Faces a portal from its first room towards its second, swapping the rooms
// if the first sits in front of the plane (bgOrderPortal).
void BgLevel::OrderPortal(size_t index) {
    BgPortal& portal = header_.portals[index];
    Vec3 center1 = RoomCenter(rooms_[portal.room1]);
    Vec3 center2 = RoomCenter(rooms_[portal.room2]);
    PortalMetric metric = MeasurePortal(portal);

    float distance1 = metric.normal[0] * center1[0] + metric.normal[1] * center1[1] + metric.normal[2] * center1[2];

    bool swapped = false;
    if (distance1 > metric.max) {
        swapped = true;
        std::swap(portal.room1, portal.room2);
        for (float& component : metric.normal) {
            component = -component;
        }
        float oldMin = metric.min;
        metric.min = -metric.max;
        metric.max = -oldMin;
    }

    float distance2 = metric.normal[0] * center2[0] + metric.normal[1] * center2[1] + metric.normal[2] * center2[2];
    // The swap is undone when the second room is behind the flipped plane too.
    if (distance2 <= metric.min && swapped) {
        std::swap(portal.room1, portal.room2);
    }
}

void BgLevel::MarkSpecialPortals() {
    for (const SpecialPortals& special : SpecialPortalTable()) {
        if (special.levelIndex != levelIndex_) {
            continue;
        }
        for (auto [first, last] : special.ranges) {
            for (size_t portal = first; portal <= last && portal < header_.portals.size(); portal++) {
                header_.portals[portal].controlBytes1 |= kPortalSpecial;
            }
        }
    }
}

} // namespace Frigate::Level
