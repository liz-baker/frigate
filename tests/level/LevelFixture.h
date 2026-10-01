#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "level/BgHeader.h"
#include "level/BgLevel.h"
#include "level/StanData.h"

// Builders for a bg header region and a source that serves it.
namespace LevelFixture {

using namespace Frigate::Level;

inline void PutU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value >> 24);
    bytes[at + 1] = static_cast<uint8_t>(value >> 16);
    bytes[at + 2] = static_cast<uint8_t>(value >> 8);
    bytes[at + 3] = static_cast<uint8_t>(value);
}

inline void PutF32(std::vector<uint8_t>& bytes, size_t at, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(bytes, at, bits);
}

inline uint32_t Addr(size_t offset) {
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
inline std::vector<uint8_t> BuildHeader(const HeaderSpec& spec) {
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
inline PortalSpec WallPortal(uint8_t room1, uint8_t room2, float x) {
    return { room1, room2, 0x01, 0, { { x, 0, 0 }, { x, 0, 2 }, { x, 2, 2 }, { x, 2, 0 } } };
}

// Points ordered so the plane's normal is +x for WallPortal-like geometry:
// (p0-p1) x (p2-p1).
inline float NormalX(const PortalSpec& portal) {
    const auto& p0 = portal.points[0];
    const auto& p1 = portal.points[1];
    const auto& p2 = portal.points[2];
    float ay = p0[1] - p1[1], az = p0[2] - p1[2];
    float by = p2[1] - p1[1], bz = p2[2] - p1[2];
    return ay * bz - az * by;
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

    std::shared_ptr<const StanData> LoadStan(const LevelEntry&) override {
        return stan;
    }

    std::vector<uint8_t> header;
    std::shared_ptr<const StanData> stan = std::make_shared<StanData>();
    std::map<int, RoomGeometry> geometries;
    std::map<int, int> loads;
    std::map<int, std::weak_ptr<void>> residentWatch;
    int failRoom = -1;
};

inline RoomGeometry Vertices(std::vector<std::array<int16_t, 3>> positions) {
    RoomGeometry geometry;
    geometry.vertexPositions = std::move(positions);
    return geometry;
}

inline void PutPad(std::vector<uint8_t>& file, size_t at, Vec3 position, Vec3 look, uint32_t name) {
    for (int axis = 0; axis < 3; axis++) {
        PutF32(file, at + axis * 4, position[axis]);
        PutF32(file, at + 0x18 + axis * 4, look[axis]);
    }
    PutF32(file, at + 0x10, 1.0f);
    PutU32(file, at + 0x24, name);
}

inline void PutWords(std::vector<uint8_t>& file, size_t at, std::initializer_list<int32_t> words) {
    for (int32_t word : words) {
        PutU32(file, at, static_cast<uint32_t>(word));
        at += 4;
    }
}

} // namespace LevelFixture
