#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// GoldenEye's table of music sequences ("GEMS"): the Rare sequence table and
// the compact sequence behind each entry. The ROM stores the sequences
// compressed; the packer decompresses them. Payload layout:
// docs/formats/goldeneye-music-sequences.md.
namespace Frigate::Audio {

// RareALSeqData.
struct MusicSequenceEntry {
    // Offset from the start of the table's file. The game adds the file's ROM
    // address to it at load (MusicSeqFileNew).
    uint32_t address = 0;
    uint16_t uncompressedLength = 0;
    // Length of the compressed data in the ROM.
    uint16_t length = 0;
    // Where the decompressed sequence starts in `data`.
    uint32_t dataOffset = 0;
};

// RareALSeqBankFile, with each entry's decompressed sequence.
struct MusicSequenceTable {
    // ROM address of the table's file.
    uint32_t romBase = 0;
    std::vector<MusicSequenceEntry> entries;
    std::vector<uint8_t> data;

    size_t Count() const {
        return entries.size();
    }
    const uint8_t* Sequence(size_t index) const {
        return data.data() + entries[index].dataOffset;
    }
    size_t SequenceSize(size_t index) const {
        return entries[index].uncompressedLength;
    }
};

// Reads a `GEMS` payload (after the resource header). False when it is
// truncated, has trailing bytes, or an entry whose sequence lies outside the
// data.
bool ParseMusicSequencesPayload(const uint8_t* data, size_t size, MusicSequenceTable* out);

// musicSeqFileNew: turns each entry's file offset into an address by adding
// `base`.
void MusicSeqFileNew(MusicSequenceTable* table, uint32_t base);

} // namespace Frigate::Audio
