#include "level/BgLevel.h"

#include <algorithm>
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

    // TODO(https://github.com/liz-baker/frigate/issues/120): lightFixtureInitTables(), which resets the stage's
    // light fixture tables before any room is read.

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

    // TODO(https://github.com/liz-baker/frigate/issues/35): the stage's stan file (stanLoadFile), which the game
    // reads here, before anything that uses the stage scale.
    // TODO(https://github.com/liz-baker/frigate/issues/122): the stage scale reaches the game's own scale globals
    // (setLevelScale, setDebugCameraScale, chrRemoved7F022E1C, and the two
    // conversion factors sub_GAME_7F0B4810 derives); they are read from
    // Level() until those systems exist.
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

    // TODO(https://github.com/liz-baker/frigate/issues/105): initializeRoomData() (bgroomtrans.c), the per-room
    // matrix slot table the renderer draws rooms through.

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

    // TODO(https://github.com/liz-baker/frigate/issues/121): fogRemoved7F0BAA5C(levelid), the stage's fog setup.
    roomLoadBudget_ = 200;
    loaded_ = true;
    return true;
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

    // TODO(https://github.com/liz-baker/frigate/issues/120): redarken_lights_in_room(room), run when the room has
    // vertices.
    // TODO(https://github.com/liz-baker/frigate/issues/121): bgApplyDynamicCCRMLUT over both lists, which swaps
    // combiner and render-mode commands for the stage's fog state.
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
        // TODO(https://github.com/liz-baker/frigate/issues/35): bounds from the stan file's room bounds; until then
        // such a room's bounds stay zero.
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
