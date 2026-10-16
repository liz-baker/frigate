#include "extract/AudioBank.h"

#include "extract/Inflate.h"

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>

namespace Frigate::Extract {

namespace {

// Largest ADPCM book the game's own books use (order 2, up to 16 predictors); a bound for rejecting a damaged count,
// not a format limit.
constexpr int32_t kMaxBookWords = 2 * 16 * 8;

// A compact MIDI sequence starts with 16 track offsets and the tick division.
constexpr size_t kCompactSequenceHeader = 0x44;
constexpr size_t kSequenceTableHeader = 4;
constexpr size_t kSequenceTableEntry = 8;

std::string Format(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

void PutU8(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
}
void PutU16(std::vector<uint8_t>& out, uint32_t v) {
    PutU8(out, v >> 8);
    PutU8(out, v);
}
void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    PutU16(out, v >> 16);
    PutU16(out, v);
}
void PutPad(std::vector<uint8_t>& out, size_t count) {
    out.insert(out.end(), count, 0);
}

// Bounds-checked big-endian reads over the `.ctl` bytes. The first failure is kept and later reads return zeros, so
// a decoder can run its course and check once.
class Reader {
  public:
    explicit Reader(std::span<const uint8_t> data) : mData(data) {
    }

    bool failed() const {
        return !mError.ok();
    }
    const Error& error() const {
        return mError;
    }
    void Fail(std::string message) {
        if (!failed()) {
            mError = Error::Make(ErrorCode::Corrupt, std::move(message));
        }
    }

    // True when `size` bytes at `offset` exist; otherwise records the failure.
    bool Has(uint64_t offset, uint64_t size) {
        if (failed()) {
            return false;
        }
        if (offset + size > mData.size()) {
            Fail(Format("read of %llu bytes at %#llx runs past the end of the bank file", (unsigned long long)size,
                        (unsigned long long)offset));
            return false;
        }
        return true;
    }

    // Reads after a successful Has() of the enclosing structure.
    uint8_t U8(uint64_t o) const {
        return mData[o];
    }
    uint16_t U16(uint64_t o) const {
        return uint16_t((mData[o] << 8) | mData[o + 1]);
    }
    uint32_t U32(uint64_t o) const {
        return (uint32_t(U16(o)) << 16) | U16(o + 2);
    }

    uint32_t CheckedU32(uint64_t o) {
        return Has(o, 4) ? U32(o) : 0;
    }

  private:
    std::span<const uint8_t> mData;
    Error mError;
};

// Gives each distinct source offset one index, decoding on first use.
template <typename T> class Interner {
  public:
    Interner(std::function<T(uint64_t)> decode, std::vector<T>& out) : mDecode(std::move(decode)), mOut(out) {
    }

    uint32_t Index(uint64_t offset) {
        auto found = mSeen.find(offset);
        if (found != mSeen.end()) {
            return found->second;
        }
        uint32_t index = uint32_t(mOut.size());
        mSeen[offset] = index;
        T value = mDecode(offset);
        mOut.push_back(std::move(value));
        return index;
    }

  private:
    std::function<T(uint64_t)> mDecode;
    std::vector<T>& mOut;
    std::unordered_map<uint64_t, uint32_t> mSeen;
};

} // namespace

Error ParseBankFile(std::span<const uint8_t> ctl, AudioBank& out, const uint64_t* tblLength) {
    out = {};
    Reader r(ctl);
    if (!r.Has(0, 4)) {
        return r.error();
    }
    int16_t revision = int16_t(r.U16(0));
    int16_t bankCount = int16_t(r.U16(2));
    if (revision != int16_t(kBankVersion)) {
        return Error::Make(ErrorCode::Corrupt, Format("bank file revision %#x is not 'B1'", revision));
    }
    if (bankCount < 0) {
        return Error::Make(ErrorCode::Corrupt, Format("bank count %d is negative", bankCount));
    }

    Interner<AudioEnvelope> envelopes(
        [&](uint64_t o) {
            AudioEnvelope e{};
            if (r.Has(o, 14)) {
                e = { int32_t(r.U32(o)), int32_t(r.U32(o + 4)), int32_t(r.U32(o + 8)), r.U8(o + 12), r.U8(o + 13) };
            }
            return e;
        },
        out.envelopes);

    Interner<AudioKeyMap> keymaps(
        [&](uint64_t o) {
            AudioKeyMap k{};
            if (r.Has(o, 6)) {
                k = { r.U8(o), r.U8(o + 1), r.U8(o + 2), r.U8(o + 3), r.U8(o + 4), int8_t(r.U8(o + 5)) };
            }
            return k;
        },
        out.keymaps);

    Interner<AudioBook> books(
        [&](uint64_t o) {
            AudioBook b{};
            if (!r.Has(o, 8)) {
                return b;
            }
            b.order = int32_t(r.U32(o));
            b.npredictors = int32_t(r.U32(o + 4));
            int64_t count = int64_t(b.order) * b.npredictors * 8;
            if (b.order < 0 || b.npredictors < 0 || count > kMaxBookWords) {
                r.Fail(Format("ADPCM book at %#llx has implausible size (order %d, %d predictors)",
                              (unsigned long long)o, b.order, b.npredictors));
                return b;
            }
            if (r.Has(o + 8, uint64_t(count) * 2)) {
                for (int64_t i = 0; i < count; i++) {
                    b.words.push_back(int16_t(r.U16(o + 8 + 2 * i)));
                }
            }
            return b;
        },
        out.books);

    auto decodeLoop = [&](uint64_t o, bool withState) {
        AudioLoop l{};
        if (!r.Has(o, 12)) {
            return l;
        }
        l.start = r.U32(o);
        l.end = r.U32(o + 4);
        l.count = r.U32(o + 8);
        // A raw wavetable's loop has no decoder state.
        if (withState && r.Has(o + 12, 32)) {
            for (int i = 0; i < 16; i++) {
                l.state[i] = int16_t(r.U16(o + 12 + 2 * i));
            }
        }
        return l;
    };
    Interner<AudioLoop> adpcmLoops([&](uint64_t o) { return decodeLoop(o, true); }, out.loops);
    Interner<AudioLoop> rawLoops([&](uint64_t o) { return decodeLoop(o, false); }, out.loops);

    Interner<AudioWaveTable> wavetables(
        [&](uint64_t o) {
            AudioWaveTable w{};
            if (!r.Has(o, 10)) {
                return w;
            }
            uint32_t base = r.U32(o);
            int32_t length = int32_t(r.U32(o + 4));
            uint8_t type = r.U8(o + 8);
            uint8_t flags = r.U8(o + 9);
            if (type != kAdpcmWave && type != kRaw16Wave) {
                r.Fail(Format("wavetable at %#llx has unknown type %d", (unsigned long long)o, type));
                return w;
            }
            if (length < 0) {
                r.Fail(Format("wavetable at %#llx has negative length %d", (unsigned long long)o, length));
                return w;
            }
            if (tblLength != nullptr && uint64_t(base) + uint64_t(length) > *tblLength) {
                r.Fail(Format("wavetable at %#llx reads %#x+%#x, past the %#llx-byte sample table",
                              (unsigned long long)o, base, uint32_t(length), (unsigned long long)*tblLength));
                return w;
            }
            uint32_t loopOffset = r.CheckedU32(o + 12);
            uint32_t book = kNoIndex;
            uint32_t loop = kNoIndex;
            if (type == kAdpcmWave) {
                uint32_t bookOffset = r.CheckedU32(o + 16);
                if (r.failed()) {
                    return w;
                }
                if (bookOffset == 0) {
                    r.Fail(Format("ADPCM wavetable at %#llx has no book", (unsigned long long)o));
                    return w;
                }
                book = books.Index(bookOffset);
                if (loopOffset != 0) {
                    loop = adpcmLoops.Index(loopOffset);
                }
            } else if (loopOffset != 0) {
                loop = rawLoops.Index(loopOffset);
            }
            return AudioWaveTable{ base, uint32_t(length), type, flags, loop, book };
        },
        out.wavetables);

    Interner<AudioSound> sounds(
        [&](uint64_t o) {
            AudioSound s{};
            if (!r.Has(o, 15)) {
                return s;
            }
            uint32_t envelope = r.U32(o);
            uint32_t keymap = r.U32(o + 4);
            uint32_t wavetable = r.U32(o + 8);
            const char* names[3] = { "envelope", "key map", "wavetable" };
            uint32_t pointers[3] = { envelope, keymap, wavetable };
            for (int i = 0; i < 3; i++) {
                if (pointers[i] == 0) {
                    r.Fail(Format("sound at %#llx has no %s", (unsigned long long)o, names[i]));
                    return s;
                }
            }
            uint32_t e = envelopes.Index(envelope);
            uint32_t k = keymaps.Index(keymap);
            uint32_t w = wavetables.Index(wavetable);
            return AudioSound{ e, k, w, r.U8(o + 12), r.U8(o + 13), r.U8(o + 14) };
        },
        out.sounds);

    Interner<AudioInstrument> instruments(
        [&](uint64_t o) {
            AudioInstrument in{};
            if (!r.Has(o, 16)) {
                return in;
            }
            in.volume = r.U8(o);
            in.pan = r.U8(o + 1);
            in.priority = r.U8(o + 2);
            in.flags = r.U8(o + 3);
            in.tremType = r.U8(o + 4);
            in.tremRate = r.U8(o + 5);
            in.tremDepth = r.U8(o + 6);
            in.tremDelay = r.U8(o + 7);
            in.vibType = r.U8(o + 8);
            in.vibRate = r.U8(o + 9);
            in.vibDepth = r.U8(o + 10);
            in.vibDelay = r.U8(o + 11);
            in.bendRange = int16_t(r.U16(o + 12));
            int16_t soundCount = int16_t(r.U16(o + 14));
            if (soundCount < 0) {
                r.Fail(Format("instrument at %#llx has negative sound count %d", (unsigned long long)o, soundCount));
                return in;
            }
            for (int i = 0; i < soundCount; i++) {
                uint32_t soundOffset = r.CheckedU32(o + 16 + 4 * uint64_t(i));
                if (r.failed()) {
                    return in;
                }
                if (soundOffset == 0) {
                    r.Fail(Format("instrument at %#llx has a null sound %d", (unsigned long long)o, i));
                    return in;
                }
                in.sounds.push_back(sounds.Index(soundOffset));
                if (r.failed()) {
                    return in;
                }
            }
            return in;
        },
        out.instruments);

    for (int b = 0; b < bankCount; b++) {
        uint32_t bankOffset = r.CheckedU32(4 + 4 * uint64_t(b));
        if (r.failed()) {
            return r.error();
        }
        if (bankOffset == 0) {
            return Error::Make(ErrorCode::Corrupt, Format("bank %d has a null offset", b));
        }
        if (!r.Has(bankOffset, 12)) {
            return r.error();
        }
        int16_t instCount = int16_t(r.U16(bankOffset));
        int32_t sampleRate = int32_t(r.U32(bankOffset + 4));
        uint32_t percussion = r.U32(bankOffset + 8);
        if (instCount < 0) {
            return Error::Make(ErrorCode::Corrupt, Format("bank %d has negative instrument count %d", b, instCount));
        }
        AudioBankEntry entry{};
        entry.sampleRate = sampleRate;
        for (int i = 0; i < instCount; i++) {
            uint32_t instOffset = r.CheckedU32(bankOffset + 12 + 4 * uint64_t(i));
            if (r.failed()) {
                return r.error();
            }
            entry.instruments.push_back(instOffset != 0 ? instruments.Index(instOffset) : kNoIndex);
            if (r.failed()) {
                return r.error();
            }
        }
        entry.percussion = percussion != 0 ? instruments.Index(percussion) : kNoIndex;
        if (r.failed()) {
            return r.error();
        }
        out.banks.push_back(std::move(entry));
    }
    return r.failed() ? r.error() : Error{};
}

void BuildBankPayload(const AudioBank& bank, std::span<const uint8_t> tbl, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(tbl.size() + 64 * 1024);
    PutU32(out, uint32_t(bank.envelopes.size()));
    PutU32(out, uint32_t(bank.keymaps.size()));
    PutU32(out, uint32_t(bank.books.size()));
    PutU32(out, uint32_t(bank.loops.size()));
    PutU32(out, uint32_t(bank.wavetables.size()));
    PutU32(out, uint32_t(bank.sounds.size()));
    PutU32(out, uint32_t(bank.instruments.size()));
    PutU32(out, uint32_t(bank.banks.size()));
    for (const AudioEnvelope& e : bank.envelopes) {
        PutU32(out, uint32_t(e.attackTime));
        PutU32(out, uint32_t(e.decayTime));
        PutU32(out, uint32_t(e.releaseTime));
        PutU8(out, e.attackVolume);
        PutU8(out, e.decayVolume);
        PutPad(out, 2);
    }
    for (const AudioKeyMap& k : bank.keymaps) {
        PutU8(out, k.velocityMin);
        PutU8(out, k.velocityMax);
        PutU8(out, k.keyMin);
        PutU8(out, k.keyMax);
        PutU8(out, k.keyBase);
        PutU8(out, uint8_t(k.detune));
        PutPad(out, 2);
    }
    for (const AudioBook& b : bank.books) {
        PutU32(out, uint32_t(b.order));
        PutU32(out, uint32_t(b.npredictors));
        for (int16_t word : b.words) {
            PutU16(out, uint16_t(word));
        }
    }
    for (const AudioLoop& l : bank.loops) {
        PutU32(out, l.start);
        PutU32(out, l.end);
        PutU32(out, l.count);
        for (int16_t s : l.state) {
            PutU16(out, uint16_t(s));
        }
    }
    for (const AudioWaveTable& w : bank.wavetables) {
        PutU32(out, w.base);
        PutU32(out, w.length);
        PutU8(out, w.type);
        PutU8(out, w.flags);
        PutPad(out, 2);
        PutU32(out, w.loop);
        PutU32(out, w.book);
    }
    for (const AudioSound& s : bank.sounds) {
        PutU32(out, s.envelope);
        PutU32(out, s.keymap);
        PutU32(out, s.wavetable);
        PutU8(out, s.samplePan);
        PutU8(out, s.sampleVolume);
        PutU8(out, s.flags);
        PutPad(out, 1);
    }
    for (const AudioInstrument& i : bank.instruments) {
        for (uint8_t v : { i.volume, i.pan, i.priority, i.flags, i.tremType, i.tremRate, i.tremDepth, i.tremDelay,
                           i.vibType, i.vibRate, i.vibDepth, i.vibDelay }) {
            PutU8(out, v);
        }
        PutU16(out, uint16_t(i.bendRange));
        PutU16(out, uint16_t(i.sounds.size()));
        for (uint32_t sound : i.sounds) {
            PutU32(out, sound);
        }
    }
    for (const AudioBankEntry& b : bank.banks) {
        PutU32(out, uint32_t(b.sampleRate));
        PutU32(out, uint32_t(b.instruments.size()));
        PutU32(out, b.percussion);
        for (uint32_t instrument : b.instruments) {
            PutU32(out, instrument);
        }
    }
    PutU32(out, uint32_t(tbl.size()));
    out.insert(out.end(), tbl.begin(), tbl.end());
}

Error PackAudioBank(std::span<const uint8_t> ctl, std::span<const uint8_t> tbl, std::vector<uint8_t>& out) {
    AudioBank bank;
    uint64_t tblLength = tbl.size();
    if (Error err = ParseBankFile(ctl, bank, &tblLength); !err.ok()) {
        return err;
    }
    BuildBankPayload(bank, tbl, out);
    return {};
}

namespace {

Error CheckCompactSequence(size_t index, const std::vector<uint8_t>& data) {
    auto fail = [&](const char* what) {
        return Error::Make(ErrorCode::Corrupt, Format("music sequence %zu %s", index, what));
    };
    if (data.size() < kCompactSequenceHeader) {
        return fail("is too short for its header");
    }
    auto u32 = [&](size_t at) {
        return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) | (uint32_t(data[at + 2]) << 8) |
               data[at + 3];
    };
    if (u32(0x40) == 0) {
        return fail("has a tick division of zero");
    }
    bool anyTrack = false;
    for (size_t i = 0; i < 16; i++) {
        uint32_t offset = u32(i * 4);
        if (offset >= data.size()) {
            return fail("has a track outside its data");
        }
        anyTrack = anyTrack || offset != 0;
    }
    if (!anyTrack) {
        return fail("has no tracks");
    }
    return {};
}

} // namespace

Error ParseMusicSequences(std::span<const uint8_t> sbk, std::vector<MusicSequence>& out) {
    out.clear();
    if (sbk.size() < kSequenceTableHeader) {
        return Error::Make(ErrorCode::Corrupt, "music sequence table is truncated");
    }
    auto u16 = [&](size_t at) { return uint32_t((sbk[at] << 8) | sbk[at + 1]); };
    auto u32 = [&](size_t at) { return (u16(at) << 16) | u16(at + 2); };
    size_t count = u16(0);
    if (kSequenceTableHeader + count * kSequenceTableEntry > sbk.size()) {
        return Error::Make(ErrorCode::Corrupt,
                           Format("music sequence table claims %zu entries but is truncated", count));
    }
    for (size_t i = 0; i < count; i++) {
        size_t entry = kSequenceTableHeader + i * kSequenceTableEntry;
        MusicSequence seq{};
        seq.address = u32(entry);
        seq.uncompressedLength = uint16_t(u16(entry + 4));
        seq.length = uint16_t(u16(entry + 6));
        if (uint64_t(seq.address) + seq.length > sbk.size()) {
            return Error::Make(ErrorCode::Corrupt, Format("music sequence %zu lies outside the table's file", i));
        }
        if (Error err = Inflate1172(sbk.subspan(seq.address, seq.length), seq.data, seq.uncompressedLength);
            !err.ok()) {
            return Error::Make(ErrorCode::Corrupt,
                               Format("music sequence %zu does not decompress: %s", i, err.message.c_str()));
        }
        if (Error err = CheckCompactSequence(i, seq.data); !err.ok()) {
            return err;
        }
        out.push_back(std::move(seq));
    }
    return {};
}

void BuildMusicPayload(uint32_t romBase, const std::vector<MusicSequence>& sequences, std::vector<uint8_t>& out) {
    out.clear();
    PutU32(out, uint32_t(sequences.size()));
    PutU32(out, romBase);
    uint32_t offset = 0;
    for (const MusicSequence& seq : sequences) {
        PutU32(out, seq.address);
        PutU16(out, seq.uncompressedLength);
        PutU16(out, seq.length);
        PutU32(out, offset);
        offset += uint32_t(seq.data.size());
    }
    PutU32(out, offset);
    for (const MusicSequence& seq : sequences) {
        out.insert(out.end(), seq.data.begin(), seq.data.end());
    }
}

Error PackMusicSequences(std::span<const uint8_t> sbk, uint32_t romBase, std::vector<uint8_t>& out) {
    std::vector<MusicSequence> sequences;
    if (Error err = ParseMusicSequences(sbk, sequences); !err.ok()) {
        return err;
    }
    BuildMusicPayload(romBase, sequences, out);
    return {};
}

} // namespace Frigate::Extract
