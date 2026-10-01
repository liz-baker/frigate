#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Builds `GEAB` payloads (docs/formats/goldeneye-audio-bank.md) for tests: no
// GoldenEye data is needed or used.
namespace Frigate::AudioTest {

class PayloadWriter {
  public:
    void U8(uint8_t v) {
        mBytes.push_back(v);
    }
    void U16(uint16_t v) {
        U8(uint8_t(v >> 8));
        U8(uint8_t(v));
    }
    void U32(uint32_t v) {
        U16(uint16_t(v >> 16));
        U16(uint16_t(v));
    }
    void S16(int16_t v) {
        U16(uint16_t(v));
    }
    void S32(int32_t v) {
        U32(uint32_t(v));
    }
    void Pad(size_t n) {
        mBytes.insert(mBytes.end(), n, 0);
    }
    void Bytes(const std::vector<uint8_t>& b) {
        mBytes.insert(mBytes.end(), b.begin(), b.end());
    }
    std::vector<uint8_t>& Data() {
        return mBytes;
    }

  private:
    std::vector<uint8_t> mBytes;
};

// One ADPCM bank with a single instrument holding `soundCount` sounds that
// share one envelope, key map and order-2 book; sound `i` plays the wavetable
// at `i * waveLength` in the sample table. Sound 0 loops when `looped`.
struct SimpleBankSpec {
    uint32_t soundCount = 1;
    uint32_t waveLength = 9;
    bool looped = false;
    int32_t decayTime = 2000;
    uint8_t keyMin = 0x05;
    uint8_t keyMax = 0x23;
    std::vector<uint8_t> samples;
};

inline std::vector<uint8_t> BuildSimpleBank(const SimpleBankSpec& spec) {
    PayloadWriter w;
    const uint32_t sampleBytes = uint32_t(spec.samples.size());
    // envelopes, keymaps, books, loops, wavetables, sounds, instruments, banks
    for (uint32_t c : { 1u, 1u, 1u, spec.looped ? 1u : 0u, spec.soundCount, spec.soundCount, 1u, 1u }) {
        w.U32(c);
    }
    w.S32(1000);
    w.S32(spec.decayTime);
    w.S32(3000);
    w.U8(127);
    w.U8(100);
    w.Pad(2);

    w.U8(0);
    w.U8(127);
    w.U8(spec.keyMin);
    w.U8(spec.keyMax);
    w.U8(60);
    w.U8(uint8_t(-3));
    w.Pad(2);

    w.S32(2);
    w.S32(1);
    for (int i = 0; i < 16; i++) {
        w.S16(int16_t(i * 100));
    }

    if (spec.looped) {
        w.U32(16);
        w.U32(spec.waveLength / 9 * 16);
        w.U32(0xFFFFFFFF);
        for (int i = 0; i < 16; i++) {
            w.S16(int16_t(i));
        }
    }

    for (uint32_t i = 0; i < spec.soundCount; i++) {
        w.U32(i * spec.waveLength);
        w.U32(spec.waveLength);
        w.U8(0);
        w.U8(0);
        w.Pad(2);
        w.U32(spec.looped && i == 0 ? 0 : 0xFFFFFFFF);
        w.U32(0);
    }

    for (uint32_t i = 0; i < spec.soundCount; i++) {
        w.U32(0);
        w.U32(0);
        w.U32(i);
        w.U8(uint8_t(64 + i));
        w.U8(100);
        w.U8(0);
        w.Pad(1);
    }

    for (uint8_t v : { 90, 64, 5, 0, 0, 1, 2, 3, 0, 4, 5, 6 }) {
        w.U8(v);
    }
    w.S16(200);
    w.U16(uint16_t(spec.soundCount));
    for (uint32_t i = 0; i < spec.soundCount; i++) {
        w.U32(i);
    }

    w.S32(22050);
    w.U32(1);
    w.U32(0xFFFFFFFF);
    w.U32(0);

    w.U32(sampleBytes);
    w.Bytes(spec.samples);
    return w.Data();
}

// One sound of a tone bank: a constant-level ADPCM wave under an envelope.
// With a zero ADPCM book a decoded sample is its 4-bit nibble shifted left by
// `shift`, so `nibble` and `shift` set the wave's level exactly.
struct SoundSpec {
    int32_t attackTime = 100000;
    int32_t decayTime = 100000;
    int32_t releaseTime = 100000;
    uint8_t attackVolume = 127;
    uint8_t decayVolume = 127;
    uint8_t velocityMin = 0;
    uint8_t velocityMax = 0;
    uint8_t keyMin = 0;
    uint8_t keyMax = 0;
    uint8_t keyBase = 60;
    int8_t detune = 0;
    uint8_t samplePan = 64;
    uint8_t sampleVolume = 127;
    int frames = 40; // 16 samples each
    uint8_t nibble = 7;
    int shift = 10;
    bool looped = false;

    int16_t Level() const {
        return int16_t((int(nibble & 0xf) << 28 >> 28) << shift);
    }
};

// A bank with one instrument holding `specs` as its sounds, in order, and a
// shared zero book.
inline std::vector<uint8_t> BuildToneBank(const std::vector<SoundSpec>& specs) {
    PayloadWriter w;
    std::vector<uint8_t> samples;
    std::vector<uint32_t> waveBase;
    uint32_t loopCount = 0;
    for (const SoundSpec& spec : specs) {
        waveBase.push_back(uint32_t(samples.size()));
        for (int f = 0; f < spec.frames; f++) {
            samples.push_back(uint8_t(spec.shift << 4));
            for (int i = 0; i < 8; i++) {
                samples.push_back(uint8_t((spec.nibble << 4) | (spec.nibble & 0xf)));
            }
        }
        loopCount += spec.looped ? 1 : 0;
    }

    const uint32_t n = uint32_t(specs.size());
    for (uint32_t c : { n, n, 1u, loopCount, n, n, 1u, 1u }) {
        w.U32(c);
    }
    for (const SoundSpec& spec : specs) {
        w.S32(spec.attackTime);
        w.S32(spec.decayTime);
        w.S32(spec.releaseTime);
        w.U8(spec.attackVolume);
        w.U8(spec.decayVolume);
        w.Pad(2);
    }
    for (const SoundSpec& spec : specs) {
        w.U8(spec.velocityMin);
        w.U8(spec.velocityMax);
        w.U8(spec.keyMin);
        w.U8(spec.keyMax);
        w.U8(spec.keyBase);
        w.U8(uint8_t(spec.detune));
        w.Pad(2);
    }
    w.S32(2);
    w.S32(1);
    for (int i = 0; i < 16; i++) {
        w.S16(0);
    }
    for (const SoundSpec& spec : specs) {
        if (spec.looped) {
            w.U32(0);
            w.U32(uint32_t(spec.frames * 16));
            w.U32(0xFFFFFFFF);
            for (int i = 0; i < 16; i++) {
                w.S16(0);
            }
        }
    }
    uint32_t loopIndex = 0;
    for (size_t i = 0; i < specs.size(); i++) {
        w.U32(waveBase[i]);
        w.U32(uint32_t(specs[i].frames * 9));
        w.U8(0);
        w.U8(0);
        w.Pad(2);
        w.U32(specs[i].looped ? loopIndex++ : 0xFFFFFFFF);
        w.U32(0);
    }
    for (uint32_t i = 0; i < n; i++) {
        w.U32(i);
        w.U32(i);
        w.U32(i);
        w.U8(specs[i].samplePan);
        w.U8(specs[i].sampleVolume);
        w.U8(0);
        w.Pad(1);
    }
    for (uint8_t v : { 90, 64, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0 }) {
        w.U8(v);
    }
    w.S16(200);
    w.U16(uint16_t(n));
    for (uint32_t i = 0; i < n; i++) {
        w.U32(i);
    }
    w.S32(22050);
    w.U32(1);
    w.U32(0xFFFFFFFF);
    w.U32(0);
    w.U32(uint32_t(samples.size()));
    w.Bytes(samples);
    return w.Data();
}

} // namespace Frigate::AudioTest
