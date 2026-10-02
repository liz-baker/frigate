#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// A GoldenEye audio bank ("GEAB"): the instruments, sounds and ADPCM data of
// one libultra bank file and its sample table. Payload layout:
// docs/formats/goldeneye-audio-bank.md. Unlike the file the game patches in
// place, structures here refer to each other by index.
namespace Frigate::Audio {

// A link that names no structure.
constexpr uint32_t kNoIndex = 0xFFFFFFFF;

enum WaveType : uint8_t {
    kAdpcmWave = 0,
    kRaw16Wave = 1,
};

// ALEnvelope. Times are in microseconds; a decay time of -1 holds until release.
struct Envelope {
    int32_t attackTime = 0;
    int32_t decayTime = 0;
    int32_t releaseTime = 0;
    uint8_t attackVolume = 0;
    uint8_t decayVolume = 0;
};

// ALKeyMap. The sound-effect player reads keyMin/keyMax as parameters (see
// SoundGroup/SoundFxMix) rather than as a key range.
struct KeyMap {
    uint8_t velocityMin = 0;
    uint8_t velocityMax = 0;
    uint8_t keyMin = 0;
    uint8_t keyMax = 0;
    uint8_t keyBase = 0;
    int8_t detune = 0;

    uint8_t SoundGroup() const {
        return keyMin & 0x3F;
    }
    uint8_t SoundFxMix() const {
        return keyMax & 0xF;
    }
};

// ALADPCMBook: `words` holds order * predictors * 8 coefficients.
struct Book {
    int32_t order = 0;
    int32_t npredictors = 0;
    std::vector<int16_t> words;
};

// ALADPCMloop (a raw wave's ALRawLoop has no state; it is zero here).
struct Loop {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t count = 0;
    int16_t state[16] = {};
};

// ALWaveTable. `base` is a byte offset into AudioBank::samples.
struct WaveTable {
    uint32_t base = 0;
    uint32_t length = 0;
    uint8_t type = kAdpcmWave;
    uint8_t flags = 0;
    uint32_t loop = kNoIndex;
    uint32_t book = kNoIndex;
};

// ALSound. Indices are into the bank's envelopes, keymaps and wavetables.
struct Sound {
    uint32_t envelope = 0;
    uint32_t keymap = 0;
    uint32_t wavetable = 0;
    uint8_t samplePan = 0;
    uint8_t sampleVolume = 0;
    uint8_t flags = 0;
};

// ALInstrument. `sounds` are indices into AudioBank::sounds.
struct Instrument {
    uint8_t volume = 0;
    uint8_t pan = 0;
    uint8_t priority = 0;
    uint8_t flags = 0;
    uint8_t tremType = 0;
    uint8_t tremRate = 0;
    uint8_t tremDepth = 0;
    uint8_t tremDelay = 0;
    uint8_t vibType = 0;
    uint8_t vibRate = 0;
    uint8_t vibDepth = 0;
    uint8_t vibDelay = 0;
    int16_t bendRange = 0;
    std::vector<uint32_t> sounds;
};

// ALBank. `instruments` are indices into AudioBank::instruments, kNoIndex for
// an empty slot.
struct Bank {
    int32_t sampleRate = 0;
    uint32_t percussion = kNoIndex;
    std::vector<uint32_t> instruments;
};

struct AudioBank {
    std::vector<Envelope> envelopes;
    std::vector<KeyMap> keymaps;
    std::vector<Book> books;
    std::vector<Loop> loops;
    std::vector<WaveTable> wavetables;
    std::vector<Sound> sounds;
    std::vector<Instrument> instruments;
    std::vector<Bank> banks;
    std::vector<uint8_t> samples;

    const Sound* FindSound(const Instrument& instrument, size_t soundIndex) const {
        if (soundIndex >= instrument.sounds.size()) {
            return nullptr;
        }
        return &sounds[instrument.sounds[soundIndex]];
    }
};

// Reads a `GEAB` payload (after the resource header). False when it is
// truncated, has trailing bytes, an index that names nothing, or a wavetable
// whose data lies outside the sample table.
bool ParseAudioBankPayload(const uint8_t* data, size_t size, AudioBank* out);

} // namespace Frigate::Audio
