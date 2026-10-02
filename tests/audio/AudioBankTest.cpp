#include "AudioBank.h"
#include "BankBuilder.h"

#include <cstdio>

using namespace Frigate::Audio;
using namespace Frigate::AudioTest;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

SimpleBankSpec Spec() {
    SimpleBankSpec spec;
    spec.soundCount = 3;
    spec.looped = true;
    for (int i = 0; i < 27; i++) {
        spec.samples.push_back(uint8_t(i));
    }
    return spec;
}

bool Parse(const std::vector<uint8_t>& bytes, AudioBank* out) {
    return ParseAudioBankPayload(bytes.data(), bytes.size(), out);
}

void TestParsesEveryTable() {
    AudioBank bank;
    Check(Parse(BuildSimpleBank(Spec()), &bank), "a well-formed payload parses");
    Check(bank.envelopes.size() == 1 && bank.keymaps.size() == 1 && bank.books.size() == 1, "table counts");
    Check(bank.wavetables.size() == 3 && bank.sounds.size() == 3 && bank.instruments.size() == 1, "table counts 2");
    Check(bank.envelopes[0].attackTime == 1000 && bank.envelopes[0].decayTime == 2000, "envelope times");
    Check(bank.envelopes[0].attackVolume == 127 && bank.envelopes[0].decayVolume == 100, "envelope volumes");
    Check(bank.keymaps[0].keyBase == 60 && bank.keymaps[0].detune == -3, "key map base and signed detune");
    Check(bank.books[0].order == 2 && bank.books[0].words.size() == 16 && bank.books[0].words[15] == 1500,
          "book coefficients");
    Check(bank.loops.size() == 1 && bank.loops[0].count == 0xFFFFFFFF && bank.loops[0].state[7] == 7, "loop");
    Check(bank.wavetables[0].loop == 0 && bank.wavetables[1].loop == kNoIndex, "only wave 0 loops");
    Check(bank.wavetables[2].base == 18 && bank.wavetables[2].length == 9, "wave base and length");
    Check(bank.sounds[2].wavetable == 2 && bank.sounds[2].samplePan == 66, "sound links");
    const Instrument& inst = bank.instruments[0];
    Check(inst.volume == 90 && inst.bendRange == 200 && inst.vibDelay == 6 && inst.sounds.size() == 3, "instrument");
    Check(bank.banks.size() == 1 && bank.banks[0].sampleRate == 22050 && bank.banks[0].percussion == kNoIndex,
          "bank header");
    Check(bank.banks[0].instruments.size() == 1 && bank.samples.size() == 27 && bank.samples[26] == 26, "samples");
    Check(bank.FindSound(inst, 1) == &bank.sounds[1] && bank.FindSound(inst, 3) == nullptr, "FindSound bounds");
}

void TestKeyMapParameterFields() {
    AudioBank bank;
    SimpleBankSpec spec = Spec();
    spec.keyMin = 0xC5;
    spec.keyMax = 0xA3;
    Check(Parse(BuildSimpleBank(spec), &bank), "parses");
    Check(bank.keymaps[0].SoundGroup() == 0x05, "group is key min's low six bits");
    Check(bank.keymaps[0].SoundFxMix() == 0x3, "fx mix is key max's low nibble");
}

void TestRejectsTruncationAtEveryLength() {
    const std::vector<uint8_t> good = BuildSimpleBank(Spec());
    bool anyAccepted = false;
    for (size_t length = 0; length < good.size(); length++) {
        AudioBank bank;
        anyAccepted |= ParseAudioBankPayload(good.data(), length, &bank);
    }
    Check(!anyAccepted, "no truncated payload parses");
}

void TestRejectsTrailingBytes() {
    std::vector<uint8_t> bytes = BuildSimpleBank(Spec());
    bytes.push_back(0);
    AudioBank bank;
    Check(!Parse(bytes, &bank), "trailing bytes after the sample table");
}

void TestRejectsWaveOutsideSamples() {
    SimpleBankSpec spec = Spec();
    spec.samples.resize(20);
    AudioBank bank;
    Check(!Parse(BuildSimpleBank(spec), &bank), "wave 2 reads past a 20-byte sample table");
}

void TestRejectsBadIndices() {
    // The instrument's sound list starts after the headers; corrupt its first
    // entry to name a sound that does not exist.
    std::vector<uint8_t> bytes = BuildSimpleBank(Spec());
    const size_t soundList = 32 + 16 + 8 + (8 + 32) + 44 + 20 * 3 + 16 * 3 + 16;
    bytes[soundList + 3] = 9;
    AudioBank bank;
    Check(!Parse(bytes, &bank), "sound index past the table");
}

void TestRejectsUnknownWaveType() {
    std::vector<uint8_t> bytes = BuildSimpleBank(Spec());
    const size_t firstWave = 32 + 16 + 8 + (8 + 32) + 44;
    bytes[firstWave + 8] = 5;
    AudioBank bank;
    Check(!Parse(bytes, &bank), "unknown wavetable type");
}

void TestRejectsHugeCountWithoutAllocating() {
    std::vector<uint8_t> bytes = BuildSimpleBank(Spec());
    bytes[0] = 0xFF; // envelope count 0xFF000001
    AudioBank bank;
    Check(!Parse(bytes, &bank), "a count larger than the payload");
}

} // namespace

int main() {
    TestParsesEveryTable();
    TestKeyMapParameterFields();
    TestRejectsTruncationAtEveryLength();
    TestRejectsTrailingBytes();
    TestRejectsWaveOutsideSamples();
    TestRejectsBadIndices();
    TestRejectsUnknownWaveType();
    TestRejectsHugeCountWithoutAllocating();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::puts("AudioBankTest passed");
    return 0;
}
