#include "extract/ModelContainer.h"

#include "extract/Inflate.h"

#include <algorithm>
#include <string>

namespace Frigate::Extract {

namespace {

constexpr uint32_t kSegmentMask = 0xFF000000;
constexpr uint32_t kOffsetMask = 0x00FFFFFF;
constexpr size_t kBgHeaderRoomTableWord = 1;

Error ContainerError(const std::string& what) {
    return Error::Make(ErrorCode::Corrupt,
                       "A model or level file in the ROM does not have the expected layout: " + what + ".");
}

uint32_t Read32(std::span<const uint8_t> data, size_t at) {
    return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) | (uint32_t(data[at + 2]) << 8) |
           uint32_t(data[at + 3]);
}

// A segment-0x0f pointer as a file offset. nullopt for a null pointer; `failed` is set for one that is not in the
// file.
std::optional<uint32_t> BgFileOffset(uint32_t pointer, size_t fileSize, bool& failed) {
    if (pointer == 0) {
        return std::nullopt;
    }
    if ((pointer & kSegmentMask) != (kBgFileSegment << 24) || (pointer & kOffsetMask) >= fileSize) {
        failed = true;
        return std::nullopt;
    }
    return pointer & kOffsetMask;
}

} // namespace

std::optional<uint32_t> ModelImage::Resolve(uint32_t address) const {
    if ((address & kSegmentMask) != kModelSegmentBase) {
        return std::nullopt;
    }
    uint32_t offset = address & kOffsetMask;
    return offset < data.size() ? std::optional<uint32_t>(offset) : std::nullopt;
}

Frigate::Extract::Resolve ModelImage::DisplayListResolver(uint32_t verticesAddress) const {
    std::optional<uint32_t> verticesAt = Resolve(verticesAddress);
    return [this, verticesAt](uint32_t address) -> std::optional<uint32_t> {
        uint32_t segment = address >> 24;
        uint32_t offset = address & kOffsetMask;
        if (segment == kModelVertexSegment) {
            return verticesAt ? std::optional<uint32_t>(*verticesAt + offset) : std::nullopt;
        }
        if (segment == kModelMatrixSegment) {
            return offset;
        }
        return Resolve(address);
    };
}

std::optional<uint32_t> BgRoom::Resolve(uint32_t address) const {
    if ((address & kSegmentMask) != (kBgRoomVertexSegment << 24)) {
        return std::nullopt;
    }
    uint32_t offset = address & kOffsetMask;
    return offset < vertices.size() ? std::optional<uint32_t>(offset) : std::nullopt;
}

Error UnwrapModelFile(std::span<const uint8_t> compressed, ModelImage& image) {
    return Inflate1172(compressed, image.data);
}

Error BgFile::Parse(std::span<const uint8_t> data, BgFile& out) {
    out = BgFile();
    out.data = data;
    if (data.size() < 8) {
        return ContainerError("a bg file is shorter than its header");
    }
    bool failed = false;
    std::optional<uint32_t> table = BgFileOffset(Read32(data, 4 * kBgHeaderRoomTableWord), data.size(), failed);
    if (failed || !table) {
        return ContainerError("a bg file has no room table");
    }

    struct Entry {
        uint32_t vertices, primary, secondary;
    };
    std::vector<Entry> entries;
    for (;;) {
        size_t entry = size_t(*table) + (entries.size() + 1) * kBgRoomEntrySize;
        if (entry + kBgRoomEntrySize > data.size()) {
            return ContainerError("a bg room table runs off the end of the file");
        }
        Entry pointers = { Read32(data, entry), Read32(data, entry + 4), Read32(data, entry + 8) };
        if (pointers.primary == 0) { // the game ends the table on a null primary list
            break;
        }
        entries.push_back(pointers);
    }

    // Where each blob can end: at the next blob, or the end of the file.
    for (const Entry& entry : entries) {
        for (uint32_t pointer : { entry.vertices, entry.primary, entry.secondary }) {
            if (pointer != 0 && (pointer & kSegmentMask) == (kBgFileSegment << 24)) {
                out.boundaries.push_back(pointer & kOffsetMask);
            }
        }
    }
    std::sort(out.boundaries.begin(), out.boundaries.end());
    out.boundaries.erase(std::unique(out.boundaries.begin(), out.boundaries.end()), out.boundaries.end());

    // The last non-null entry is not a room: the game reads its pointers only to size the final room's blobs.
    for (size_t i = 0; i + 1 < entries.size(); i++) {
        Room room;
        bool bad = false;
        room.vertices = BgFileOffset(entries[i].vertices, data.size(), bad);
        std::optional<uint32_t> primary = BgFileOffset(entries[i].primary, data.size(), bad);
        room.secondary = BgFileOffset(entries[i].secondary, data.size(), bad);
        if (bad || !primary) {
            return ContainerError("a bg room points outside its file");
        }
        room.primary = *primary;
        out.rooms.push_back(room);
    }

    size_t firstEntry = size_t(*table) + kBgRoomEntrySize;
    if (firstEntry + 4 > data.size()) {
        return ContainerError("a bg room table runs off the end of the file");
    }
    failed = false;
    std::optional<uint32_t> firstVertices = BgFileOffset(Read32(data, firstEntry), data.size(), failed);
    if (failed || !firstVertices) {
        return ContainerError("a bg file's first room has no vertex blob to size the header by");
    }
    size_t headerSize = size_t(((*firstVertices - 1) | 0xF) + 1);
    out.header = data.subspan(0, std::min(headerSize, data.size()));
    return {};
}

Error BgFile::InflateBlob(uint32_t offset, std::vector<uint8_t>& out) const {
    auto next = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
    size_t end = next != boundaries.end() ? std::min<size_t>(*next, data.size()) : data.size();
    Error err = Inflate1172(data.subspan(offset, end - offset), out);
    if (!err.ok() && end != data.size()) {
        err = Inflate1172(data.subspan(offset), out);
    }
    return err;
}

Error BgFile::InflateRoom(size_t n, BgRoom& room) const {
    const Room& source = rooms[n];
    room = BgRoom();
    room.index = uint32_t(n + 1);
    if (source.vertices) {
        if (Error err = InflateBlob(*source.vertices, room.vertices); !err.ok()) {
            return err;
        }
    }
    if (Error err = InflateBlob(source.primary, room.primary); !err.ok()) {
        return err;
    }
    if (source.secondary) {
        room.hasSecondary = true;
        if (Error err = InflateBlob(*source.secondary, room.secondary); !err.ok()) {
            return err;
        }
    }
    return {};
}

} // namespace Frigate::Extract
