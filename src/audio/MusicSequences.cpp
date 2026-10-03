#include "MusicSequences.h"

#include <utility>

namespace Frigate::Audio {

namespace {

constexpr size_t kHeaderBytes = 8;
constexpr size_t kEntryBytes = 12;

uint32_t U32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint16_t U16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

} // namespace

bool ParseMusicSequencesPayload(const uint8_t* data, size_t size, MusicSequenceTable* out) {
    if (size < kHeaderBytes) {
        return false;
    }
    const uint32_t count = U32(data);
    const uint32_t romBase = U32(data + 4);

    if (count > (size - kHeaderBytes) / kEntryBytes) {
        return false;
    }
    const size_t dataSizeAt = kHeaderBytes + size_t(count) * kEntryBytes;
    if (size - dataSizeAt < 4) {
        return false;
    }
    const uint32_t dataSize = U32(data + dataSizeAt);
    if (size - dataSizeAt - 4 != dataSize) {
        return false;
    }

    MusicSequenceTable table;
    table.romBase = romBase;
    table.entries.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* p = data + kHeaderBytes + size_t(i) * kEntryBytes;
        MusicSequenceEntry& entry = table.entries[i];
        entry.address = U32(p);
        entry.uncompressedLength = U16(p + 4);
        entry.length = U16(p + 6);
        entry.dataOffset = U32(p + 8);
        if (entry.dataOffset > dataSize || entry.uncompressedLength > dataSize - entry.dataOffset) {
            return false;
        }
    }
    table.data.assign(data + dataSizeAt + 4, data + size);
    *out = std::move(table);
    return true;
}

void MusicSeqFileNew(MusicSequenceTable* table, uint32_t base) {
    for (MusicSequenceEntry& entry : table->entries) {
        entry.address += base;
    }
}

} // namespace Frigate::Audio
