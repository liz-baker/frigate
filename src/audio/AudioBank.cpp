#include "AudioBank.h"

#include <limits>
#include <utility>

namespace Frigate::Audio {

namespace {

// Bounds-checked big-endian reads; any read past the end latches `ok` false
// and returns zero, so a caller checks once after a run of reads.
class Reader {
  public:
    Reader(const uint8_t* data, size_t size) : mData(data), mSize(size) {
    }

    uint32_t U32() {
        if (!Take(4)) {
            return 0;
        }
        const uint8_t* p = mData + mCursor - 4;
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    uint16_t U16() {
        if (!Take(2)) {
            return 0;
        }
        const uint8_t* p = mData + mCursor - 2;
        return uint16_t((p[0] << 8) | p[1]);
    }
    uint8_t U8() {
        return Take(1) ? mData[mCursor - 1] : 0;
    }
    int32_t S32() {
        return static_cast<int32_t>(U32());
    }
    int16_t S16() {
        return static_cast<int16_t>(U16());
    }
    void Skip(size_t count) {
        Take(count);
    }

    // Whether `count` more elements of `elementSize` bytes remain.
    bool Fits(size_t count, size_t elementSize) const {
        return mOk && count <= (mSize - mCursor) / elementSize;
    }
    bool Ok() const {
        return mOk;
    }
    size_t Remaining() const {
        return mSize - mCursor;
    }
    const uint8_t* Cursor() const {
        return mData + mCursor;
    }

  private:
    bool Take(size_t count) {
        if (!mOk || count > mSize - mCursor) {
            mOk = false;
            return false;
        }
        mCursor += count;
        return true;
    }

    const uint8_t* mData;
    size_t mSize;
    size_t mCursor = 0;
    bool mOk = true;
};

// A link is none, or an index into a table of `count`.
bool ValidLink(uint32_t link, size_t count, bool allowNone) {
    return link < count || (allowNone && link == kNoIndex);
}

} // namespace

bool ParseAudioBankPayload(const uint8_t* data, size_t size, AudioBank* out) {
    Reader r(data, size);
    uint32_t counts[8];
    for (uint32_t& count : counts) {
        count = r.U32();
    }
    if (!r.Ok()) {
        return false;
    }
    const uint32_t envelopeCount = counts[0], keymapCount = counts[1], bookCount = counts[2];
    const uint32_t loopCount = counts[3], waveCount = counts[4], soundCount = counts[5];
    const uint32_t instrumentCount = counts[6], bankCount = counts[7];

    AudioBank bank;

    if (!r.Fits(envelopeCount, 16)) {
        return false;
    }
    bank.envelopes.resize(envelopeCount);
    for (Envelope& e : bank.envelopes) {
        e.attackTime = r.S32();
        e.decayTime = r.S32();
        e.releaseTime = r.S32();
        e.attackVolume = r.U8();
        e.decayVolume = r.U8();
        r.Skip(2);
    }

    if (!r.Fits(keymapCount, 8)) {
        return false;
    }
    bank.keymaps.resize(keymapCount);
    for (KeyMap& k : bank.keymaps) {
        k.velocityMin = r.U8();
        k.velocityMax = r.U8();
        k.keyMin = r.U8();
        k.keyMax = r.U8();
        k.keyBase = r.U8();
        k.detune = static_cast<int8_t>(r.U8());
        r.Skip(2);
    }

    // A book is at least its two header words.
    if (!r.Fits(bookCount, 8)) {
        return false;
    }
    bank.books.resize(bookCount);
    for (Book& b : bank.books) {
        b.order = r.S32();
        b.npredictors = r.S32();
        if (b.order < 0 || b.npredictors < 0 ||
            static_cast<uint64_t>(b.order) * static_cast<uint64_t>(b.npredictors) >
                std::numeric_limits<uint32_t>::max() / 16 ||
            !r.Fits(static_cast<size_t>(b.order) * static_cast<size_t>(b.npredictors) * 8, 2)) {
            return false;
        }
        b.words.resize(static_cast<size_t>(b.order) * static_cast<size_t>(b.npredictors) * 8);
        for (int16_t& word : b.words) {
            word = r.S16();
        }
    }

    if (!r.Fits(loopCount, 44)) {
        return false;
    }
    bank.loops.resize(loopCount);
    for (Loop& l : bank.loops) {
        l.start = r.U32();
        l.end = r.U32();
        l.count = r.U32();
        for (int16_t& word : l.state) {
            word = r.S16();
        }
    }

    if (!r.Fits(waveCount, 20)) {
        return false;
    }
    bank.wavetables.resize(waveCount);
    for (WaveTable& w : bank.wavetables) {
        w.base = r.U32();
        w.length = r.U32();
        w.type = r.U8();
        w.flags = r.U8();
        r.Skip(2);
        w.loop = r.U32();
        w.book = r.U32();
        if ((w.type != kAdpcmWave && w.type != kRaw16Wave) || !ValidLink(w.loop, loopCount, true) ||
            !ValidLink(w.book, bookCount, w.type != kAdpcmWave)) {
            return false;
        }
    }

    if (!r.Fits(soundCount, 16)) {
        return false;
    }
    bank.sounds.resize(soundCount);
    for (Sound& s : bank.sounds) {
        s.envelope = r.U32();
        s.keymap = r.U32();
        s.wavetable = r.U32();
        s.samplePan = r.U8();
        s.sampleVolume = r.U8();
        s.flags = r.U8();
        r.Skip(1);
        if (!ValidLink(s.envelope, envelopeCount, false) || !ValidLink(s.keymap, keymapCount, false) ||
            !ValidLink(s.wavetable, waveCount, false)) {
            return false;
        }
    }

    // An instrument is at least its 16-byte header.
    if (!r.Fits(instrumentCount, 16)) {
        return false;
    }
    bank.instruments.resize(instrumentCount);
    for (Instrument& i : bank.instruments) {
        i.volume = r.U8();
        i.pan = r.U8();
        i.priority = r.U8();
        i.flags = r.U8();
        i.tremType = r.U8();
        i.tremRate = r.U8();
        i.tremDepth = r.U8();
        i.tremDelay = r.U8();
        i.vibType = r.U8();
        i.vibRate = r.U8();
        i.vibDepth = r.U8();
        i.vibDelay = r.U8();
        i.bendRange = r.S16();
        const uint16_t count = r.U16();
        if (!r.Fits(count, 4)) {
            return false;
        }
        i.sounds.resize(count);
        for (uint32_t& index : i.sounds) {
            index = r.U32();
            if (!ValidLink(index, soundCount, false)) {
                return false;
            }
        }
    }

    // A bank is at least its three header words.
    if (!r.Fits(bankCount, 12)) {
        return false;
    }
    bank.banks.resize(bankCount);
    for (Bank& b : bank.banks) {
        b.sampleRate = r.S32();
        const uint32_t count = r.U32();
        b.percussion = r.U32();
        if (!ValidLink(b.percussion, instrumentCount, true) || !r.Fits(count, 4)) {
            return false;
        }
        b.instruments.resize(count);
        for (uint32_t& index : b.instruments) {
            index = r.U32();
            if (!ValidLink(index, instrumentCount, true)) {
                return false;
            }
        }
    }

    const uint32_t sampleBytes = r.U32();
    if (!r.Ok() || sampleBytes != r.Remaining()) {
        return false;
    }
    bank.samples.assign(r.Cursor(), r.Cursor() + sampleBytes);

    for (const WaveTable& w : bank.wavetables) {
        if (static_cast<uint64_t>(w.base) + w.length > sampleBytes) {
            return false;
        }
    }

    *out = std::move(bank);
    return true;
}

} // namespace Frigate::Audio
