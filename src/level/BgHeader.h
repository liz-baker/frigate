#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// A bg file's header region as the packer stores it (`bg/<stem>/header`).
// Layout: docs/formats/goldeneye-containers.md.
namespace Frigate::Level {

// The most rooms a stage can have (MAXROOMCOUNT, US version).
constexpr int kMaxRoomCount = 150;
// The most portals a stage can have (PORTMAX).
constexpr int kMaxPortalCount = 200;

using Vec3 = std::array<float, 3>;

// A room table entry. Addresses are segment-0x0f addresses, 0 when absent.
struct BgRoomEntry {
    uint32_t vertexBlob = 0;
    uint32_t primaryList = 0;
    uint32_t secondaryList = 0;
    Vec3 position{};
};

// A doorway between two rooms.
struct BgPortal {
    // Where the portal's point list sits in the header region, as an address.
    uint32_t address = 0;
    uint8_t room1 = 0;
    uint8_t room2 = 0;
    uint8_t controlBytes1 = 0;
    uint8_t controlBytes2 = 0;
    std::vector<Vec3> points;
};

// Control byte 1's flags (PORTALFLAG_*). The disabled flag is cleared once a
// stage has loaded.
constexpr uint8_t kPortalDisabled = 0x01;
constexpr uint8_t kPortalSpecial = 0x02;

// A portal's plane: its unit normal and the range of the points' distances
// along it (sub_GAME_7F0B96CC).
struct PortalMetric {
    Vec3 normal;
    float min;
    float max;
};

PortalMetric MeasurePortal(const BgPortal& portal);

// How far from a portal's plane the viewer still counts as standing in it,
// from control byte 2 (sub_GAME_7F0B9990).
float PortalPadding(const BgPortal& portal);

struct BgHeader {
    // The region itself. The visibility data (`visibilityOffset`) stays in it,
    // as the game parses it in place.
    std::vector<uint8_t> bytes;
    // Word 0 of the file; anything but 0 marks a file the game does not set
    // rooms up for.
    uint32_t firstWord = 0;
    // Index 0 is a placeholder. Real rooms are 1..maxNumRooms-1; the entry at
    // maxNumRooms is not a room (it only holds end pointers).
    std::vector<BgRoomEntry> rooms;
    int maxNumRooms = 0;
    std::vector<BgPortal> portals;
    // Offsets into `bytes`; 0 when the file has none.
    uint32_t visibilityOffset = 0;
    uint32_t visibilityFloatsOffset = 0;
};

// Reads the header out of `bytes`; nullopt if it points outside the region or
// holds more rooms or portals than the game has room for.
std::optional<BgHeader> ParseBgHeader(std::vector<uint8_t> bytes);

// A segment-0x0f address as an offset into the region; nullopt for anything
// else or an offset past `size`.
std::optional<uint32_t> BgAddressToOffset(uint32_t address, size_t size);

} // namespace Frigate::Level
