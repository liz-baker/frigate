#include "level/BgHeader.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace Frigate::Level {

namespace {

constexpr uint32_t kFileSegment = 0x0F;
constexpr size_t kRoomEntrySize = 0x18;
constexpr size_t kPortalEntrySize = 8;
constexpr size_t kPortalPointSize = 12;
// The visibility data starts with 8-byte records whose first byte is a type
// and whose last word is an argument, ended by a type of 0.
constexpr size_t kVisibilityRecordSize = 8;
constexpr uint8_t kVisibilityTypeAlt = 100;

// Header words: 1 = room table, 2 = portal table, 3 = visibility data, 4 =
// floats the visibility data indexes.
constexpr size_t kRoomTableWord = 1;
constexpr size_t kPortalTableWord = 2;
constexpr size_t kVisibilityWord = 3;
constexpr size_t kVisibilityFloatsWord = 4;
constexpr size_t kHeaderWordCount = 5;

uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t offset) {
    return (uint32_t{ bytes[offset] } << 24) | (uint32_t{ bytes[offset + 1] } << 16) |
           (uint32_t{ bytes[offset + 2] } << 8) | uint32_t{ bytes[offset + 3] };
}

void WriteU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}

float ReadF32(const std::vector<uint8_t>& bytes, size_t offset) {
    uint32_t bits = ReadU32(bytes, offset);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool Fits(const std::vector<uint8_t>& bytes, size_t offset, size_t length) {
    return offset <= bytes.size() && length <= bytes.size() - offset;
}

std::optional<Vec3> ReadVec3(const std::vector<uint8_t>& bytes, size_t offset) {
    if (!Fits(bytes, offset, kPortalPointSize)) {
        return std::nullopt;
    }
    return Vec3{ ReadF32(bytes, offset), ReadF32(bytes, offset + 4), ReadF32(bytes, offset + 8) };
}

bool ReadRoomTable(const std::vector<uint8_t>& bytes, BgHeader& header) {
    auto table = BgAddressToOffset(ReadU32(bytes, kRoomTableWord * 4), bytes.size());
    if (!table) {
        return false;
    }
    // Entry 0 is a placeholder; the table ends at the first entry with no
    // primary list.
    for (size_t index = 0;; index++) {
        size_t at = *table + index * kRoomEntrySize;
        if (!Fits(bytes, at, kRoomEntrySize)) {
            return false;
        }
        BgRoomEntry entry;
        entry.vertexBlob = ReadU32(bytes, at);
        entry.primaryList = ReadU32(bytes, at + 4);
        entry.secondaryList = ReadU32(bytes, at + 8);
        if (index > 0 && entry.primaryList == 0) {
            break;
        }
        entry.position = { ReadF32(bytes, at + 12), ReadF32(bytes, at + 16), ReadF32(bytes, at + 20) };
        header.rooms.push_back(entry);
        if (header.rooms.size() > kMaxRoomCount + 1) {
            return false;
        }
    }
    header.maxNumRooms = static_cast<int>(header.rooms.size()) - 1;
    return true;
}

bool ReadPortalTable(const std::vector<uint8_t>& bytes, BgHeader& header) {
    auto table = BgAddressToOffset(ReadU32(bytes, kPortalTableWord * 4), bytes.size());
    if (!table) {
        return false;
    }
    for (size_t index = 0;; index++) {
        size_t at = *table + index * kPortalEntrySize;
        if (!Fits(bytes, at, kPortalEntrySize)) {
            return false;
        }
        uint32_t address = ReadU32(bytes, at);
        if (address == 0) {
            return true;
        }
        if (index >= kMaxPortalCount) {
            return false;
        }

        BgPortal portal;
        portal.address = address;
        portal.room1 = bytes[at + 4];
        portal.room2 = bytes[at + 5];
        portal.controlBytes1 = bytes[at + 6];
        portal.controlBytes2 = bytes[at + 7];

        auto pointsAt = BgAddressToOffset(address, bytes.size());
        if (!pointsAt || !Fits(bytes, *pointsAt, 4)) {
            return false;
        }
        // One count byte and three of padding, then the points.
        size_t count = bytes[*pointsAt];
        for (size_t i = 0; i < count; i++) {
            auto point = ReadVec3(bytes, *pointsAt + 4 + i * kPortalPointSize);
            if (!point) {
                return false;
            }
            portal.points.push_back(*point);
        }
        header.portals.push_back(std::move(portal));
    }
}

// Rewrites each ALT record's argument from the address of a portal to the
// portal's index (0 when no portal sits there), in place.
bool ResolveVisibilityPortals(BgHeader& header) {
    for (size_t at = header.visibilityOffset;; at += kVisibilityRecordSize) {
        if (!Fits(header.bytes, at, kVisibilityRecordSize)) {
            return false;
        }
        uint8_t type = header.bytes[at];
        if (type == 0) {
            return true;
        }
        if (type != kVisibilityTypeAlt) {
            continue;
        }
        uint32_t address = ReadU32(header.bytes, at + 4);
        uint32_t index = 0;
        for (size_t i = 0; i < header.portals.size(); i++) {
            if (header.portals[i].address == address) {
                index = static_cast<uint32_t>(i);
                break;
            }
        }
        WriteU32(header.bytes, at + 4, index);
    }
}

} // namespace

PortalMetric MeasurePortal(const BgPortal& portal) {
    const Vec3& p0 = portal.points[0];
    const Vec3& p1 = portal.points[1];
    const Vec3& p2 = portal.points[2];

    Vec3 a{ p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2] };
    Vec3 b{ p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };

    PortalMetric metric;
    metric.normal = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };

    float length = std::sqrt(metric.normal[0] * metric.normal[0] + metric.normal[1] * metric.normal[1] +
                             metric.normal[2] * metric.normal[2]);
    if (length != 0.0f) {
        length = 1.0f / length;
    }
    for (float& component : metric.normal) {
        component *= length;
    }

    metric.min = std::numeric_limits<float>::max();
    metric.max = -std::numeric_limits<float>::max();
    for (const Vec3& point : portal.points) {
        float distance = point[0] * metric.normal[0] + point[1] * metric.normal[1] + point[2] * metric.normal[2];
        metric.min = distance < metric.min ? distance : metric.min;
        metric.max = distance > metric.max ? distance : metric.max;
    }
    return metric;
}

float PortalPadding(const BgPortal& portal) {
    float padding = static_cast<float>(portal.controlBytes2 & 0xF) * 0.25f;
    for (int shift = (portal.controlBytes2 >> 4) & 0xF; shift != 0; shift--) {
        padding += padding;
    }
    return padding;
}

std::optional<uint32_t> BgAddressToOffset(uint32_t address, size_t size) {
    if ((address >> 24) != kFileSegment || (address & 0x00FFFFFF) >= size) {
        return std::nullopt;
    }
    return address & 0x00FFFFFF;
}

std::optional<BgHeader> ParseBgHeader(std::vector<uint8_t> bytes) {
    if (bytes.size() < kHeaderWordCount * 4) {
        return std::nullopt;
    }

    BgHeader header;
    header.firstWord = ReadU32(bytes, 0);
    // Only files with a zero first word get rooms; the rest are left as the
    // game leaves them, with nothing to load.
    if (header.firstWord != 0) {
        header.bytes = std::move(bytes);
        return header;
    }

    if (!ReadRoomTable(bytes, header) || !ReadPortalTable(bytes, header)) {
        return std::nullopt;
    }

    uint32_t visibilityAddress = ReadU32(bytes, kVisibilityWord * 4);
    if (visibilityAddress != 0) {
        auto visibility = BgAddressToOffset(visibilityAddress, bytes.size());
        if (!visibility) {
            return std::nullopt;
        }
        header.visibilityOffset = *visibility;

        uint32_t floatsAddress = ReadU32(bytes, kVisibilityFloatsWord * 4);
        if (floatsAddress != 0) {
            auto floats = BgAddressToOffset(floatsAddress, bytes.size());
            if (!floats) {
                return std::nullopt;
            }
            header.visibilityFloatsOffset = *floats;
        }
    }

    header.bytes = std::move(bytes);
    if (header.visibilityOffset != 0 && !ResolveVisibilityPortals(header)) {
        return std::nullopt;
    }
    return header;
}

} // namespace Frigate::Level
