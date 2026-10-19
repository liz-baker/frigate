#pragma once

#include "extract/Error.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frigate::Extract {

// Decoders and payload builders for the GEAB (audio bank) and GEMS (music sequences) resources.
// Layouts: docs/formats/goldeneye-audio-bank.md, docs/formats/goldeneye-music-sequences.md.

constexpr uint16_t kBankVersion = 0x4231; // 'B1'
constexpr uint8_t kAdpcmWave = 0;
constexpr uint8_t kRaw16Wave = 1;
constexpr uint32_t kNoIndex = 0xFFFFFFFF;

struct AudioEnvelope {
    int32_t attackTime, decayTime, releaseTime;
    uint8_t attackVolume, decayVolume;
};

struct AudioKeyMap {
    uint8_t velocityMin, velocityMax, keyMin, keyMax, keyBase;
    int8_t detune;
};

struct AudioBook {
    int32_t order, npredictors;
    std::vector<int16_t> words;
};

struct AudioLoop {
    uint32_t start, end, count;
    int16_t state[16];
};

struct AudioWaveTable {
    uint32_t base, length; // into the sample table
    uint8_t type, flags;
    uint32_t loop; // index into loops, or kNoIndex
    uint32_t book; // index into books, or kNoIndex (raw waves have none)
};

struct AudioSound {
    uint32_t envelope, keymap, wavetable;
    uint8_t samplePan, sampleVolume, flags;
};

struct AudioInstrument {
    uint8_t volume, pan, priority, flags;
    uint8_t tremType, tremRate, tremDepth, tremDelay;
    uint8_t vibType, vibRate, vibDepth, vibDelay;
    int16_t bendRange;
    std::vector<uint32_t> sounds;
};

struct AudioBankEntry {
    int32_t sampleRate;
    uint32_t percussion;               // index into instruments, or kNoIndex
    std::vector<uint32_t> instruments; // indices; kNoIndex for an empty slot
};

struct AudioBank {
    std::vector<AudioEnvelope> envelopes;
    std::vector<AudioKeyMap> keymaps;
    std::vector<AudioBook> books;
    std::vector<AudioLoop> loops;
    std::vector<AudioWaveTable> wavetables;
    std::vector<AudioSound> sounds;
    std::vector<AudioInstrument> instruments;
    std::vector<AudioBankEntry> banks;
};

// Decodes a `.ctl`. With `tblLength`, every wavetable's sample data must lie inside a table of that size.
Error ParseBankFile(std::span<const uint8_t> ctl, AudioBank& out, const uint64_t* tblLength = nullptr);
// The flat tables, then the sample table.
void BuildBankPayload(const AudioBank& bank, std::span<const uint8_t> tbl, std::vector<uint8_t>& out);
Error PackAudioBank(std::span<const uint8_t> ctl, std::span<const uint8_t> tbl, std::vector<uint8_t>& out);

struct MusicSequence {
    uint32_t address; // of the compressed data from the start of the table
    uint16_t uncompressedLength;
    uint16_t length;           // compressed
    std::vector<uint8_t> data; // the decompressed compact sequence
};

Error ParseMusicSequences(std::span<const uint8_t> sbk, std::vector<MusicSequence>& out);
void BuildMusicPayload(uint32_t romBase, const std::vector<MusicSequence>& sequences, std::vector<uint8_t>& out);
// `romBase` is the ROM offset `sbk` starts at.
Error PackMusicSequences(std::span<const uint8_t> sbk, uint32_t romBase, std::vector<uint8_t>& out);

} // namespace Frigate::Extract
