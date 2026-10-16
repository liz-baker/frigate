// Unit tests for the audio packer (src/extract/AudioBank.*, AudioPack.*), ported from
// tools/o2r_packer/test_ge_audio.py. They run on synthetic bank files; the ROM-gated check at the end needs the US
// ROM in FRIGATE_ROMS and skips otherwise.

#include "extract/AudioBank.h"
#include "extract/AudioNames.h"
#include "extract/AudioPack.h"
#include "extract/Extractor.h"
#include "extract/Inflate.h"
#include "extract/Region.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

using Bytes = std::vector<uint8_t>;

void U8(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v));
}
void U16(Bytes& b, uint32_t v) {
    U8(b, v >> 8);
    U8(b, v);
}
void U32(Bytes& b, uint32_t v) {
    U16(b, v >> 16);
    U16(b, v);
}
uint32_t ReadU32(const Bytes& b, size_t at) {
    return (uint32_t(b[at]) << 24) | (uint32_t(b[at + 1]) << 16) | (uint32_t(b[at + 2]) << 8) | b[at + 3];
}

// Lays out a `.ctl` the way the file stores it: structures at byte offsets from the file start, linked by those
// offsets.
class CtlBuilder {
  public:
    CtlBuilder() : data(4 + 4 * 4, 0) { // room for a header with up to 4 banks
    }

    uint32_t Add(const Bytes& blob) {
        while (data.size() % 4) {
            data.push_back(0);
        }
        uint32_t offset = uint32_t(data.size());
        data.insert(data.end(), blob.begin(), blob.end());
        return offset;
    }

    uint32_t Envelope(int32_t attack = 1000, int32_t decay = 2000, int32_t release = 3000, uint8_t av = 127,
                      uint8_t dv = 100) {
        Bytes b;
        U32(b, attack);
        U32(b, decay);
        U32(b, release);
        U8(b, av);
        U8(b, dv);
        U16(b, 0);
        return Add(b);
    }

    uint32_t KeyMap(uint8_t keyMin = 0, uint8_t keyMax = 0x2F, uint8_t keyBase = 60, int8_t detune = -3) {
        Bytes b;
        for (uint8_t v :
             { uint8_t(0), uint8_t(127), keyMin, keyMax, keyBase, uint8_t(detune), uint8_t(0), uint8_t(0) }) {
            U8(b, v);
        }
        return Add(b);
    }

    uint32_t Book(int order = 2, int npredictors = 1) {
        Bytes b;
        U32(b, order);
        U32(b, npredictors);
        for (int i = 0; i < order * npredictors * 8; i++) {
            U16(b, i);
        }
        return Add(b);
    }

    uint32_t Loop(uint32_t start = 16, uint32_t end = 160, uint32_t count = 0xFFFFFFFF) {
        Bytes b;
        U32(b, start);
        U32(b, end);
        U32(b, count);
        for (int i = 0; i < 16; i++) {
            U16(b, i);
        }
        return Add(b);
    }

    uint32_t AdpcmWave(uint32_t base, uint32_t length, uint32_t loop = 0, uint32_t book = 0) {
        return Wave(base, length, kAdpcmWave, loop, book, true);
    }
    uint32_t RawWave(uint32_t base, uint32_t length, uint32_t loop = 0) {
        return Wave(base, length, kRaw16Wave, loop, 0, false);
    }
    uint32_t Wave(uint32_t base, uint32_t length, uint8_t type, uint32_t loop, uint32_t book, bool hasBook) {
        Bytes b;
        U32(b, base);
        U32(b, length);
        U8(b, type);
        U8(b, 0);
        U16(b, 0);
        U32(b, loop);
        if (hasBook) {
            U32(b, book);
        }
        return Add(b);
    }

    uint32_t Sound(uint32_t envelope, uint32_t keymap, uint32_t wavetable, uint8_t pan = 64, uint8_t volume = 100) {
        Bytes b;
        U32(b, envelope);
        U32(b, keymap);
        U32(b, wavetable);
        U8(b, pan);
        U8(b, volume);
        U8(b, 0);
        U8(b, 0);
        return Add(b);
    }

    uint32_t Instrument(const std::vector<uint32_t>& sounds, uint8_t volume = 90, int16_t bend = 200) {
        Bytes b;
        for (uint8_t v : { volume, uint8_t(64), uint8_t(5), uint8_t(0), uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3),
                           uint8_t(0), uint8_t(4), uint8_t(5), uint8_t(6) }) {
            U8(b, v);
        }
        U16(b, uint16_t(bend));
        U16(b, uint32_t(sounds.size()));
        for (uint32_t s : sounds) {
            U32(b, s);
        }
        return Add(b);
    }

    uint32_t Bank(const std::vector<uint32_t>& instruments, uint32_t percussion = 0, int32_t sampleRate = 22050) {
        Bytes b;
        U16(b, uint32_t(instruments.size()));
        U8(b, 0);
        U8(b, 0);
        U32(b, uint32_t(sampleRate));
        U32(b, percussion);
        for (uint32_t i : instruments) {
            U32(b, i);
        }
        return Add(b);
    }

    // Writes the header for `banks` and returns the file.
    Bytes Finish(const std::vector<uint32_t>& banks, uint16_t revision = kBankVersion) {
        Bytes header;
        U16(header, revision);
        U16(header, uint32_t(banks.size()));
        for (uint32_t b : banks) {
            U32(header, b);
        }
        std::copy(header.begin(), header.end(), data.begin());
        return data;
    }

    Bytes data;
};

Bytes SimpleCtl() {
    CtlBuilder b;
    uint32_t env = b.Envelope();
    uint32_t key = b.KeyMap();
    uint32_t book = b.Book();
    uint32_t loop = b.Loop();
    uint32_t waveA = b.AdpcmWave(0, 72, loop, book);
    uint32_t waveB = b.AdpcmWave(72, 36, 0, book); // same book, no loop
    uint32_t raw = b.RawWave(108, 20);
    uint32_t sounds[3] = { b.Sound(env, key, waveA), b.Sound(env, key, waveB, 10), b.Sound(env, key, raw, 64, 50) };
    uint32_t inst = b.Instrument({ sounds[0], sounds[1], sounds[2] });
    return b.Finish({ b.Bank({ inst }) });
}

// A bank holding one ADPCM sound.
Bytes OneSoundCtl(int32_t decay, bool nullInstrumentSlot, uint32_t* instOut = nullptr) {
    CtlBuilder b;
    uint32_t sound = b.Sound(b.Envelope(1000, decay), b.KeyMap(), b.AdpcmWave(0, 8, 0, b.Book()));
    uint32_t inst = b.Instrument({ sound });
    if (instOut != nullptr) {
        *instOut = inst;
    }
    return b.Finish({ nullInstrumentSlot ? b.Bank({ inst, 0 }, inst) : b.Bank({ inst }) });
}

Bytes Tbl(size_t n) {
    Bytes out(n);
    for (size_t i = 0; i < n; i++) {
        out[i] = uint8_t(i);
    }
    return out;
}

void TestParseBank() {
    AudioBank bank;
    Check(ParseBankFile(SimpleCtl(), bank).ok(), "simple bank parses");
    Check(bank.banks.size() == 1 && bank.banks[0].sampleRate == 22050 && bank.banks[0].percussion == kNoIndex &&
              bank.banks[0].instruments == std::vector<uint32_t>{ 0 },
          "bank entry");
    Check(bank.instruments.size() == 1 && bank.instruments[0].volume == 90 && bank.instruments[0].bendRange == 200 &&
              bank.instruments[0].sounds == std::vector<uint32_t>{ 0, 1, 2 },
          "instrument");
    const AudioInstrument& inst = bank.instruments[0];
    Check(inst.tremType == 0 && inst.tremRate == 1 && inst.tremDepth == 2 && inst.vibType == 0 && inst.vibDelay == 6,
          "instrument effects");

    Check(bank.envelopes.size() == 1 && bank.keymaps.size() == 1 && bank.books.size() == 1 &&
              bank.wavetables.size() == 3,
          "shared structures stored once");
    Check(bank.sounds[0].envelope == 0 && bank.sounds[1].envelope == 0 && bank.sounds[2].envelope == 0,
          "sounds share the envelope");

    const AudioEnvelope& env = bank.envelopes[0];
    Check(env.attackTime == 1000 && env.decayTime == 2000 && env.releaseTime == 3000 && env.attackVolume == 127 &&
              env.decayVolume == 100,
          "envelope fields");
    const AudioKeyMap& key = bank.keymaps[0];
    Check(key.keyMin == 0 && key.keyMax == 0x2F && key.keyBase == 60 && key.detune == -3, "keymap fields");
    const AudioBook& book = bank.books[0];
    Check(book.order == 2 && book.npredictors == 1 && book.words.size() == 16 && book.words[15] == 15, "book fields");
    const AudioWaveTable& wave = bank.wavetables[0];
    Check(wave.base == 0 && wave.length == 72 && wave.type == kAdpcmWave, "wavetable fields");
    Check(bank.loops[wave.loop].start == 16 && bank.loops[wave.loop].end == 160 && bank.loops[wave.loop].state[7] == 7,
          "loop fields");
    Check(bank.wavetables[1].loop == kNoIndex, "wave without a loop");
    Check(bank.sounds[1].samplePan == 10, "sound pan");
    const AudioWaveTable& raw = bank.wavetables[2];
    Check(raw.type == kRaw16Wave && raw.book == kNoIndex && raw.loop == kNoIndex, "raw wave has no book or loop");
}

void TestParseBankEdges() {
    AudioBank bank;
    Check(ParseBankFile(OneSoundCtl(-1, false), bank).ok() && bank.envelopes[0].decayTime == -1,
          "negative decay time survives");

    Check(ParseBankFile(OneSoundCtl(1, true), bank).ok() && bank.banks[0].instruments.size() == 2 &&
              bank.banks[0].instruments[1] == kNoIndex && bank.banks[0].percussion == 0,
          "null instrument slot and percussion");

    {
        CtlBuilder b;
        uint32_t sound = b.Sound(b.Envelope(), b.KeyMap(), b.AdpcmWave(0, 8, 0, b.Book()));
        uint32_t inst = b.Instrument({ sound });
        Bytes file = b.Finish({ b.Bank({ inst }), b.Bank({ inst }, 0, 11025) });
        Check(ParseBankFile(file, bank).ok() && bank.instruments.size() == 1 && bank.banks.size() == 2 &&
                  bank.banks[0].sampleRate == 22050 && bank.banks[1].sampleRate == 11025,
              "two banks share instruments");
    }

    Bytes wrongRevision = SimpleCtl();
    wrongRevision[0] = 0x42;
    wrongRevision[1] = 0x32;
    Check(!ParseBankFile(wrongRevision, bank).ok(), "wrong revision rejected");

    Bytes truncated = SimpleCtl();
    truncated.resize(40);
    Check(!ParseBankFile(truncated, bank).ok(), "truncated file rejected");
    Check(!ParseBankFile(Bytes{ 0x42 }, bank).ok(), "file shorter than its header rejected");

    uint64_t small = 100;
    uint64_t exact = 128;
    Check(!ParseBankFile(SimpleCtl(), bank, &small).ok(), "sample data past the table rejected");
    Check(ParseBankFile(SimpleCtl(), bank, &exact).ok(), "sample data inside the table accepted");

    CtlBuilder b;
    uint32_t wave = b.Wave(0, 8, 7, 0, 0, true);
    uint32_t sound = b.Sound(b.Envelope(), b.KeyMap(), wave);
    Check(!ParseBankFile(b.Finish({ b.Bank({ b.Instrument({ sound }) }) }), bank).ok(), "unknown wave type rejected");

    CtlBuilder noBook;
    uint32_t noBookSound = noBook.Sound(noBook.Envelope(), noBook.KeyMap(), noBook.AdpcmWave(0, 8, 0, 0));
    Check(!ParseBankFile(noBook.Finish({ noBook.Bank({ noBook.Instrument({ noBookSound }) }) }), bank).ok(),
          "ADPCM wave without a book rejected");
}

void TestBankPayload() {
    Bytes tbl = Tbl(128);
    Bytes payload;
    Check(PackAudioBank(SimpleCtl(), tbl, payload).ok(), "bank packs");
    const uint32_t counts[8] = { 1, 1, 1, 1, 3, 3, 1, 1 }; // envelopes ... banks
    bool countsMatch = true;
    for (size_t i = 0; i < 8; i++) {
        countsMatch = countsMatch && ReadU32(payload, i * 4) == counts[i];
    }
    Check(countsMatch, "payload counts");
    Check(ReadU32(payload, payload.size() - 128 - 4) == 128, "sample table length");
    Check(Bytes(payload.end() - 128, payload.end()) == tbl, "payload ends with the sample table");

    size_t offset = 32;
    offset += 16;         // one envelope
    offset += 8;          // one keymap
    offset += 8 + 16 * 2; // one order-2 book of 16 words
    offset += 44;         // one loop
    Check(ReadU32(payload, offset) == 0 && ReadU32(payload, offset + 4) == 72 && payload[offset + 8] == kAdpcmWave &&
              ReadU32(payload, offset + 12) == 0,
          "first wavetable at its documented offset");
    offset += 20 * 3;
    Check(ReadU32(payload, offset) == 0 && payload[offset + 12] == 64 && payload[offset + 13] == 100,
          "first sound at its documented offset");

    Check(!PackAudioBank(SimpleCtl(), Tbl(100), payload).ok(), "PackAudioBank checks the sample table size");

    AudioBank empty;
    Check(ParseBankFile(CtlBuilder().Finish({}), empty).ok(), "empty bank parses");
    BuildBankPayload(empty, {}, payload);
    Check(payload.size() == 36 && ReadU32(payload, 0) == 0 && ReadU32(payload, 28) == 0 && ReadU32(payload, 32) == 0,
          "empty bank round-trips the counts");
}

// A compact MIDI sequence with one track right after the header.
Bytes CompactSequence(uint32_t division = 480, Bytes tail = { 0x00, 0xFF, 0x2F, 0x00 }) {
    Bytes b;
    U32(b, 0x44);
    for (int i = 0; i < 15; i++) {
        U32(b, 0);
    }
    U32(b, division);
    b.insert(b.end(), tail.begin(), tail.end());
    return b;
}

// A sequence table and its compressed sequences, as `music.sbk` lays them out.
Bytes SimpleSbk(const std::vector<Bytes>& sequences) {
    std::vector<Bytes> packed;
    for (const Bytes& seq : sequences) {
        Bytes deflated;
        DeflateRaw(seq, deflated);
        Bytes comp{ 0x11, 0x72 };
        comp.insert(comp.end(), deflated.begin(), deflated.end());
        packed.push_back(comp);
    }
    Bytes table;
    U16(table, uint32_t(sequences.size()));
    U16(table, 0);
    uint32_t cursor = 4 + 8 * uint32_t(sequences.size());
    Bytes body;
    for (size_t i = 0; i < sequences.size(); i++) {
        U32(table, cursor + uint32_t(body.size()));
        U16(table, uint32_t(sequences[i].size()));
        U16(table, uint32_t(packed[i].size()));
        body.insert(body.end(), packed[i].begin(), packed[i].end());
    }
    table.insert(table.end(), body.begin(), body.end());
    return table;
}

void TestMusic() {
    Bytes seq0 = CompactSequence();
    Bytes seq1 = CompactSequence(96, { 1, 2, 3, 4, 5, 0xFF, 0x2F, 0 });
    std::vector<MusicSequence> parsed;
    Check(ParseMusicSequences(SimpleSbk({ seq0, seq1 }), parsed).ok() && parsed.size() == 2 && parsed[0].data == seq0 &&
              parsed[1].data == seq1 && parsed[0].address == 4 + 16 && parsed[1].uncompressedLength == seq1.size(),
          "sequences are decompressed");

    Bytes payload;
    Check(PackMusicSequences(SimpleSbk({ seq0, seq1 }), 0x419790, payload).ok(), "music packs");
    Check(ReadU32(payload, 0) == 2 && ReadU32(payload, 4) == 0x419790, "payload header");
    Check(ReadU32(payload, 8) == 4 + 16 && ReadU32(payload, 16) == 0, "first entry address and offset");
    Check(ReadU32(payload, 28) == seq0.size(), "second entry offset is the first sequence's length");
    Check(ReadU32(payload, 32) == seq0.size() + seq1.size(), "data size");
    Bytes both = seq0;
    both.insert(both.end(), seq1.begin(), seq1.end());
    Check(Bytes(payload.begin() + 36, payload.end()) == both, "payload ends with the sequences");

    Bytes wrongLength = SimpleSbk({ seq0 });
    wrongLength[9] = 5;
    Check(!ParseMusicSequences(wrongLength, parsed).ok(), "a length that disagrees is rejected");

    Bytes tableOnly;
    U16(tableOnly, 3);
    U16(tableOnly, 0);
    Check(!ParseMusicSequences(tableOnly, parsed).ok(), "a truncated table is rejected");
    Check(!ParseMusicSequences(Bytes{ 0 }, parsed).ok(), "a one-byte table is rejected");

    Bytes outside = SimpleSbk({ seq0 });
    outside[4] = 0x00;
    outside[5] = 0x01;
    Check(!ParseMusicSequences(outside, parsed).ok(), "a sequence outside the file is rejected");

    Bytes garbage = SimpleSbk({ seq0 });
    garbage[4 + 8] = 0;
    Check(!ParseMusicSequences(garbage, parsed).ok(), "garbage compression is rejected");

    Bytes tooShort(8, 0);
    Bytes zeroDivision = CompactSequence(0);
    Bytes outsideTrack;
    U32(outsideTrack, 0x1000);
    for (int i = 0; i < 15; i++) {
        U32(outsideTrack, 0);
    }
    U32(outsideTrack, 96);
    Bytes noTracks(0x40, 0);
    U32(noTracks, 96);
    for (const Bytes& bad : { tooShort, zeroDivision, outsideTrack, noTracks }) {
        Check(!ParseMusicSequences(SimpleSbk({ bad }), parsed).ok(), "a damaged sequence header is rejected");
    }
}

std::string AsString(const Bytes& b) {
    return std::string(b.begin(), b.end());
}

void TestNames() {
    const AudioName names[] = { { "NOTHING_SFX", 0 }, { "FIRST_SFX", 1 }, { "SECOND_SFX", 2 }, { "THIRD_SFX", 3 } };
    Check(AsString(BuildAudioNamesPayload(names)) == "NOTHING_SFX\nFIRST_SFX\nSECOND_SFX\nTHIRD_SFX", "names by value");

    const AudioName gaps[] = { { "A_SFX", 0 }, { "B_SFX", 3 }, { "C_SFX", 4 }, { "D_SFX", 7 } };
    Check(AsString(BuildAudioNamesPayload(gaps)) == "A_SFX\n\n\nB_SFX\nC_SFX\n\n\nD_SFX", "gaps are empty lines");
    Check(BuildAudioNamesPayload({}).empty(), "no names, no payload");

    Check(Detail::kSfxNamesCount > 100 && std::string(Detail::kSfxNames[0].name) == "NOTHING_SFX",
          "generated SFX table");
    Check(Detail::kMusicNamesCount > 10 && std::string(Detail::kMusicNames[0].name) == "M_NONE",
          "generated music track table");
}

std::optional<fs::path> FindUsRom() {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        fs::path location = list.substr(start, end - start);
        start = end + 1;
        std::error_code ec;
        if (fs::is_regular_file(location, ec) && IsUsRom(location)) {
            return location;
        }
        if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                if (entry.is_regular_file() && IsUsRom(entry.path())) {
                    return entry.path();
                }
            }
        }
    }
    return std::nullopt;
}

void TestRom() {
    std::optional<fs::path> path = FindUsRom();
    if (!path) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    std::ifstream file(*path, std::ios::binary);
    Bytes rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const RomLayout& layout = GetRomLayout(Region::Us);
    StageContext context(Region::Us, rom, nullptr, nullptr);

    std::span<const uint8_t> ctl;
    std::span<const uint8_t> tbl;
    const FileEntry* sfxCtl = layout.FindFile("assets/music/sfx.ctl");
    const FileEntry* sfxTbl = layout.FindFile("assets/music/sfx.tbl");
    Check(sfxCtl && sfxTbl && context.RomRange(sfxCtl->offset, sfxCtl->size, ctl).ok() &&
              context.RomRange(sfxTbl->offset, sfxTbl->size, tbl).ok(),
          "sfx bank files are in the ROM");
    AudioBank bank;
    Check(ParseBankFile(ctl, bank, nullptr).ok() && !bank.sounds.empty() && !bank.banks.empty(),
          "the ROM's sfx bank parses");

    fs::path dir = fs::temp_directory_path() / "frigate_audio_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    O2rWriter writer;
    Check(writer.Open(dir / "audio.o2r").ok(), "archive opens");
    Check(PackAudio(context, writer).ok(), "the audio stage runs on the ROM");
    Check(writer.MemberCount() == 5, "the audio stage writes five members");
    Check(writer.Close().ok(), "archive closes");
    fs::remove_all(dir);
}

} // namespace

int main() {
    TestParseBank();
    TestParseBankEdges();
    TestBankPayload();
    TestMusic();
    TestNames();
    TestRom();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
