#include "AudioBank.h"
#include "AudioEngine.h"
#include "BankBuilder.h"
#include "CompactSeq.h"
#include "MusicSequences.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Checks the music path on synthetic data: the compact sequence decoder, the
// sequence table reader, and the music players driving the synth, with the
// PCM measured. No GoldenEye data is used. The synthetic instrument is a
// looped constant-level wave, so output level and timing, not pitch, show
// whether a note sounded.

using namespace Frigate::Audio;
using namespace Frigate::AudioDriver;
using namespace Frigate::AudioTest;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// ---------------------------------------------------------------------------
// Sequence building

constexpr uint32_t kDivision = 96;
constexpr uint32_t kHeaderBytes = 0x44;

void VarLen(std::vector<uint8_t>& t, uint32_t value) {
    std::vector<uint8_t> parts = { uint8_t(value & 0x7F) };
    for (value >>= 7; value != 0; value >>= 7) {
        parts.push_back(uint8_t(0x80 | (value & 0x7F)));
    }
    t.insert(t.end(), parts.rbegin(), parts.rend());
}

void Bytes(std::vector<uint8_t>& t, std::initializer_list<uint8_t> bytes) {
    t.insert(t.end(), bytes);
}

void NoteOn(std::vector<uint8_t>& t, uint32_t delta, uint8_t key, uint8_t velocity, uint32_t duration,
            uint8_t channel = 0) {
    VarLen(t, delta);
    Bytes(t, { uint8_t(0x90 | channel), key, velocity });
    VarLen(t, duration);
}

void EndOfTrack(std::vector<uint8_t>& t, uint32_t delta) {
    VarLen(t, delta);
    Bytes(t, { 0xFF, 0x2F });
}

void Tempo(std::vector<uint8_t>& t, uint32_t delta, uint32_t microsPerQuarter) {
    VarLen(t, delta);
    Bytes(t,
          { 0xFF, 0x51, uint8_t(microsPerQuarter >> 16), uint8_t(microsPerQuarter >> 8), uint8_t(microsPerQuarter) });
}

void U32(std::vector<uint8_t>& t, uint32_t v) {
    Bytes(t, { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) });
}

// A sequence of `tracks`, each starting right after the previous.
std::vector<uint8_t> BuildSequence(const std::vector<std::vector<uint8_t>>& tracks, uint32_t division = kDivision) {
    std::vector<uint8_t> out;
    uint32_t offset = kHeaderBytes;
    for (int i = 0; i < 16; i++) {
        if (size_t(i) < tracks.size()) {
            U32(out, offset);
            offset += uint32_t(tracks[size_t(i)].size());
        } else {
            U32(out, 0);
        }
    }
    U32(out, division);
    for (const auto& track : tracks) {
        out.insert(out.end(), track.begin(), track.end());
    }
    return out;
}

// ---------------------------------------------------------------------------
// The compact sequence

struct Decoded {
    std::vector<SeqEvent> events;
    std::vector<int32_t> deltas;
};

// Reads every event of `seq` the way the player does: the delta, then the event.
Decoded Decode(std::vector<uint8_t>& data, size_t limit = 64) {
    CompactSeq seq;
    seq.New(data.data(), data.size());
    Decoded out;
    int32_t delta;
    while (out.events.size() < limit && seq.NextDelta(&delta)) {
        SeqEvent event;
        seq.NextEvent(&event);
        out.deltas.push_back(delta);
        out.events.push_back(event);
        if (event.type == kSeqEndEvt) {
            break;
        }
    }
    return out;
}

void TestSeqReadsNotesTempoAndEnd() {
    std::vector<uint8_t> t;
    Tempo(t, 0, 400000);
    NoteOn(t, 0, 60, 100, 48);
    // Running status: no status byte, so key and velocity follow directly.
    VarLen(t, 24);
    Bytes(t, { 62, 90 });
    VarLen(t, 10);
    // Program change takes one data byte.
    VarLen(t, 5);
    Bytes(t, { 0xC3, 7 });
    EndOfTrack(t, 100);
    std::vector<uint8_t> data = BuildSequence({ t });

    CompactSeq seq;
    seq.New(data.data(), data.size());
    Check(std::fabs(seq.Qnpt() - 1.0f / 96.0f) < 1e-9f, "ticks per quarter note");

    Decoded d = Decode(data);
    Check(d.events.size() == 5, "five events");
    if (d.events.size() != 5) {
        return;
    }
    Check(d.events[0].type == kTempoEvt && d.events[0].tempo.byte1 == 0x06 && d.events[0].tempo.byte2 == 0x1A &&
              d.events[0].tempo.byte3 == 0x80,
          "tempo event bytes");
    Check(d.events[1].type == kSeqMidiEvt && d.events[1].midi.status == 0x90 && d.events[1].midi.byte1 == 60 &&
              d.events[1].midi.byte2 == 100 && d.events[1].midi.duration == 48,
          "note on with a duration");
    Check(d.events[2].midi.status == 0x90 && d.events[2].midi.byte1 == 62 && d.events[2].midi.byte2 == 90 &&
              d.events[2].midi.duration == 10,
          "running status reuses the last status");
    Check(d.events[3].midi.status == 0xC3 && d.events[3].midi.byte1 == 7 && d.events[3].midi.byte2 == 0,
          "program change has one data byte");
    Check(d.events[4].type == kSeqEndEvt, "the last end of track ends the sequence");
    Check(d.deltas[0] == 0 && d.deltas[1] == 0 && d.deltas[2] == 24 && d.deltas[3] == 5 && d.deltas[4] == 100,
          "delta ticks");
}

void TestSeqMergesTracksByTime() {
    std::vector<uint8_t> a, b;
    NoteOn(a, 0, 60, 100, 4);
    NoteOn(a, 50, 61, 100, 4);
    EndOfTrack(a, 10);
    NoteOn(b, 20, 70, 100, 4);
    EndOfTrack(b, 10);
    std::vector<uint8_t> data = BuildSequence({ a, b });
    Decoded d = Decode(data);

    std::vector<int> keys;
    for (const SeqEvent& e : d.events) {
        keys.push_back(e.type == kSeqMidiEvt ? e.midi.byte1 : -1);
    }
    // Track a at 0, track b at 20, then each track's end at 30 and (a's second
    // note at 50 comes after b ends at 30) a's note, then a's end.
    Check(keys == std::vector<int>({ 60, 70, -1, 61, -1 }), "events come in time order across tracks");
    Check(d.events.back().type == kSeqEndEvt && d.events[2].type == kTrackEnd,
          "a track end is not the sequence end while another track remains");
    Check(d.deltas[1] == 20 && d.deltas[2] == 10, "deltas are relative to the previous event");
}

void TestSeqBackReferencesAndEscapedBlockCode() {
    std::vector<uint8_t> t;
    // First event: 5 bytes. Second: a back-reference to those 5 bytes.
    NoteOn(t, 3, 64, 80, 7);
    Bytes(t, { 0xFE, 0x00, 0x05, 0x05 });
    // A doubled block code reads as a literal 0xFE: here the first byte of a
    // two-byte delta, (0x7E << 7) + 0 ticks.
    Bytes(t, { 0xFE, 0xFE, 0x00, 0x90, 65, 70, 1 });
    EndOfTrack(t, 0);
    std::vector<uint8_t> data = BuildSequence({ t });
    Decoded d = Decode(data);

    Check(d.events.size() == 4, "four events");
    if (d.events.size() != 4) {
        return;
    }
    Check(d.events[1].type == kSeqMidiEvt && d.events[1].midi.byte1 == 64 && d.events[1].midi.byte2 == 80 &&
              d.events[1].midi.duration == 7 && d.deltas[1] == 3,
          "a back-reference replays earlier bytes, delta included");
    Check(d.events[2].midi.byte1 == 65 && d.deltas[2] == (0x7E << 7), "a doubled block code is a literal byte");
}

void TestSeqLoopsTheStatedNumberOfTimes() {
    std::vector<uint8_t> t;
    // Loop start, a note, then a loop end that jumps back once.
    VarLen(t, 0);
    Bytes(t, { 0xFF, 0x2E, 0, 0 });
    const uint32_t afterLoopStart = kHeaderBytes + uint32_t(t.size());
    NoteOn(t, 0, 60, 100, 4);
    VarLen(t, 16);
    Bytes(t, { 0xFF, 0x2D, 1, 1 });
    const uint32_t distPosition = kHeaderBytes + uint32_t(t.size());
    U32(t, distPosition + 4 - afterLoopStart);
    EndOfTrack(t, 16);
    std::vector<uint8_t> data = BuildSequence({ t });
    Decoded d = Decode(data);

    std::vector<int> types;
    for (const SeqEvent& e : d.events) {
        types.push_back(e.type);
    }
    // Start, note, end (jumps), note, end (falls through), end of sequence.
    Check(types == std::vector<int>({ kCspLoopStart, kSeqMidiEvt, kCspLoopEnd, kSeqMidiEvt, kCspLoopEnd, kSeqEndEvt }),
          "a loop with one repeat plays its body twice");

    // A loop count of 0xFF repeats without end, and does not count down.
    std::vector<uint8_t> forever;
    VarLen(forever, 0);
    Bytes(forever, { 0xFF, 0x2E, 0, 0 });
    const uint32_t foreverStart = kHeaderBytes + uint32_t(forever.size());
    NoteOn(forever, 0, 60, 100, 4);
    VarLen(forever, 16);
    Bytes(forever, { 0xFF, 0x2D, 0xFF, 0xFF });
    U32(forever, kHeaderBytes + uint32_t(forever.size()) + 4 - foreverStart);
    EndOfTrack(forever, 16);
    std::vector<uint8_t> foreverData = BuildSequence({ forever });
    Decoded f = Decode(foreverData, 40);
    Check(f.events.size() == 40 && f.events.back().type != kSeqEndEvt, "a forever loop never ends");
}

// ---------------------------------------------------------------------------
// The sequence table

std::vector<uint8_t> BuildTablePayload(uint32_t romBase, const std::vector<std::vector<uint8_t>>& sequences) {
    PayloadWriter w;
    w.U32(uint32_t(sequences.size()));
    w.U32(romBase);
    uint32_t offset = 0;
    for (size_t i = 0; i < sequences.size(); i++) {
        w.U32(0x1000 + uint32_t(i) * 0x100);
        w.U16(uint16_t(sequences[i].size()));
        w.U16(uint16_t(sequences[i].size() / 2));
        w.U32(offset);
        offset += uint32_t(sequences[i].size());
    }
    w.U32(offset);
    for (const auto& s : sequences) {
        w.Bytes(s);
    }
    return w.Data();
}

void TestTableParses() {
    const std::vector<std::vector<uint8_t>> sequences = { { 1, 2, 3 }, { 4, 5 }, {} };
    const std::vector<uint8_t> payload = BuildTablePayload(0x419790, sequences);
    MusicSequenceTable table;
    Check(ParseMusicSequencesPayload(payload.data(), payload.size(), &table), "a well-formed table parses");
    Check(table.Count() == 3 && table.romBase == 0x419790, "count and ROM base");
    Check(table.entries[1].address == 0x1100 && table.entries[1].uncompressedLength == 2 &&
              table.entries[1].length == 1,
          "entry fields");
    Check(table.SequenceSize(0) == 3 && table.Sequence(0)[2] == 3 && table.Sequence(1)[0] == 4, "sequence data");
    Check(table.SequenceSize(2) == 0, "an empty sequence");

    MusicSeqFileNew(&table, 0x10000);
    Check(table.entries[0].address == 0x11000 && table.entries[1].address == 0x11100,
          "MusicSeqFileNew adds the base to each address");
}

void TestTableRejectsDamage() {
    const std::vector<uint8_t> good = BuildTablePayload(0, { { 1, 2, 3 }, { 4, 5 } });
    MusicSequenceTable table;
    Check(!ParseMusicSequencesPayload(good.data(), 7, &table), "a truncated header");
    for (size_t cut : { size_t(20), size_t(good.size() - 1) }) {
        Check(!ParseMusicSequencesPayload(good.data(), cut, &table), "a truncated table");
    }
    std::vector<uint8_t> trailing = good;
    trailing.push_back(0);
    Check(!ParseMusicSequencesPayload(trailing.data(), trailing.size(), &table), "trailing bytes");

    std::vector<uint8_t> outside = good;
    outside[8 + 4 + 1] = 0xFF;
    Check(!ParseMusicSequencesPayload(outside.data(), outside.size(), &table), "a sequence longer than the data");

    std::vector<uint8_t> hugeCount = good;
    hugeCount[0] = 0x7F;
    Check(!ParseMusicSequencesPayload(hugeCount.data(), hugeCount.size(), &table), "a count the file cannot hold");
}

// ---------------------------------------------------------------------------
// Players on the synth

struct Peak {
    int left = 0;
    int right = 0;
    int Level() const {
        return std::max(left, right);
    }
};

Peak PeakOf(const std::vector<int16_t>& frame) {
    Peak p;
    for (size_t i = 0; i + 1 < frame.size(); i += 2) {
        p.left = std::max(p.left, std::abs(int(frame[i])));
        p.right = std::max(p.right, std::abs(int(frame[i + 1])));
    }
    return p;
}

constexpr double kFrameSeconds = double(kFrameSamples) / kOutputRate;

struct Rig {
    AudioBank sfx;
    AudioBank instruments;
    MusicSequenceTable sequences;
    std::unique_ptr<AudioEngine> engine;
    std::vector<int16_t> frame = std::vector<int16_t>(kFrameSamples * 2);

    // A bank whose one instrument is a looped constant tone covering every
    // key, and a table of `tracks` (track 0 unused).
    explicit Rig(const std::vector<std::vector<uint8_t>>& tracks, uint32_t romBase = 0x419790) {
        SoundSpec spec;
        spec.keyMin = 0;
        spec.keyMax = 127;
        spec.velocityMin = 0;
        spec.velocityMax = 127;
        spec.attackTime = 20000;
        spec.decayTime = 20000;
        spec.releaseTime = 100000;
        spec.looped = true;
        const std::vector<uint8_t> bank = BuildToneBank({ spec });
        Check(ParseAudioBankPayload(bank.data(), bank.size(), &sfx), "the tone bank parses");
        instruments = sfx;

        sequences.romBase = romBase;
        for (size_t i = 0; i < tracks.size(); i++) {
            MusicSequenceEntry entry;
            entry.address = 0x20000 + uint32_t(i) * 0x100;
            entry.uncompressedLength = uint16_t(tracks[i].size());
            entry.length = uint16_t(tracks[i].size());
            entry.dataOffset = uint32_t(sequences.data.size());
            sequences.entries.push_back(entry);
            sequences.data.insert(sequences.data.end(), tracks[i].begin(), tracks[i].end());
        }

        MusicAssets assets;
        assets.instruments = &instruments;
        assets.sequences = &sequences;
        engine = std::make_unique<AudioEngine>(sfx, EngineConfig(), &assets);
    }

    MusicPlayer& Music() {
        return engine->Music();
    }

    Peak Step() {
        engine->RenderFrame(frame.data());
        return PeakOf(frame);
    }

    // Renders `seconds`, returning the peak of each frame.
    std::vector<Peak> Run(double seconds) {
        std::vector<Peak> peaks;
        for (double t = 0; t < seconds; t += kFrameSeconds) {
            peaks.push_back(Step());
        }
        return peaks;
    }

    // Seconds from the start of `peaks` to the last frame that is not silent.
    static double SoundedFor(const std::vector<Peak>& peaks) {
        size_t last = 0;
        for (size_t i = 0; i < peaks.size(); i++) {
            if (peaks[i].Level() != 0) {
                last = i + 1;
            }
        }
        return double(last) * kFrameSeconds;
    }
};

// A track that plays one note for `ticks` and ends with it.
std::vector<uint8_t> OneNote(uint32_t ticks, bool loopForever = false, uint32_t tempoMicros = 0) {
    std::vector<uint8_t> t;
    if (loopForever) {
        VarLen(t, 0);
        Bytes(t, { 0xFF, 0x2E, 0, 0 });
    }
    const uint32_t loopStart = kHeaderBytes + uint32_t(t.size());
    if (tempoMicros != 0) {
        Tempo(t, 0, tempoMicros);
    }
    NoteOn(t, 0, 60, 100, ticks);
    if (loopForever) {
        VarLen(t, ticks);
        Bytes(t, { 0xFF, 0x2D, 0xFF, 0xFF });
        U32(t, kHeaderBytes + uint32_t(t.size()) + 4 - loopStart);
        EndOfTrack(t, 0);
    } else {
        EndOfTrack(t, ticks);
    }
    return BuildSequence({ t });
}

void TestTrackPlaysAndEnds() {
    // 96 ticks at the default 500000 us per quarter note: half a second.
    Rig rig({ OneNote(2), OneNote(96) });
    Check(rig.Step().Level() == 0, "silent before anything plays");
    Check(rig.Music().SeqState(0) == kSeqStopped && rig.Music().CurrentTrack(0) == 0, "stopped at first");

    Check(rig.Music().TrackPlay(0, 1), "a track starts");
    Check(rig.Music().CurrentTrack(0) == 1, "the track number reads back");
    const std::vector<Peak> peaks = rig.Run(2.0);

    Check(peaks[1].Level() > 500, "the note sounds");
    const double sounded = Rig::SoundedFor(peaks);
    Check(sounded > 0.5 && sounded < 0.9, "the note lasts its duration plus its release");
    Check(rig.Music().SeqState(0) == kSeqStopped, "the player stops when the sequence ends");
    Check(rig.Music().Player(0).ActiveVoices() == 0, "no voices are left");
    Check(rig.Music().CurrentTrack(0) == 1, "the track number stays until the slot is stopped");
}

void TestTempoChangeRescalesTime() {
    // The same note, with a tempo event at the start doubling the speed.
    Rig rig({ OneNote(2), OneNote(96, false, 250000) });
    rig.Music().TrackPlay(0, 1);
    const std::vector<Peak> peaks = rig.Run(2.0);
    const double sounded = Rig::SoundedFor(peaks);
    Check(sounded > 0.25 && sounded < 0.5, "a faster tempo shortens the note");
    Check(rig.Music().Player(0).MicrosPerTick() == int32_t(250000.0f * (1.0f / 96.0f)), "microseconds per tick");
}

void TestLoopedTrackKeepsPlayingUntilStopped() {
    Rig rig({ OneNote(2), OneNote(96, true) });
    rig.Music().TrackPlay(0, 1);
    const std::vector<Peak> peaks = rig.Run(6.0);
    size_t silent = 0;
    for (size_t i = 4; i < peaks.size(); i++) {
        silent += peaks[i].Level() == 0;
    }
    Check(rig.Music().SeqState(0) == kSeqPlaying, "a looping track is still playing");
    Check(silent == 0, "and stays audible");

    rig.Music().TrackStop(0);
    Check(rig.Music().CurrentTrack(0) == 0, "stop clears the track number");
    const std::vector<Peak> after = rig.Run(1.0);
    Check(rig.Music().SeqState(0) == kSeqStopped, "stop brings the player to a halt");
    Check(after.back().Level() == 0, "and it falls silent");
    Check(Rig::SoundedFor(after) < 0.3, "within the voices' kill time");
}

void TestVolumeScalesOutput() {
    Rig rig({ OneNote(2), OneNote(96, true) });
    rig.Music().TrackPlay(0, 1);
    const std::vector<Peak> full = rig.Run(1.0);
    const int fullLevel = full.back().Level();

    rig.Music().TrackApplySeqpVol(0, kMusicVolumeMax / 2);
    const std::vector<Peak> half = rig.Run(1.0);
    const int halfLevel = half.back().Level();
    Check(fullLevel > 1000, "a loud note");
    // The synth's volume curve is quadratic: half the volume is a quarter of the level.
    Check(halfLevel > fullLevel * 0.2 && halfLevel < fullLevel * 0.32, "half the volume is a quarter of the level");
    Check(rig.Music().TrackGetVolume(0) == kMusicVolumeMax / 2, "the slot volume reads back");

    // The player runs at the slot volume scaled by the track's default.
    const int16_t def = rig.Music().TrackDefaultVolume(1);
    Check(rig.Music().Player(0).Volume() == int16_t((uint32_t(kMusicVolumeMax / 2) * uint32_t(def)) >> 15),
          "the player volume is the slot volume scaled by the track default");
}

void TestFadeOutStopsThePlayer() {
    Rig rig({ OneNote(2), OneNote(96, true) });
    rig.Music().TrackPlay(0, 1);
    rig.Run(0.5);

    rig.Music().TrackFadeOut(0, 0.5f);
    Check(rig.Music().FadeState(0) == kMusicFadeOut, "fading out");
    int lastLevel = 1 << 30;
    bool decreasing = true;
    // 60 ticks of fade per second, two per frame, until it ends.
    for (int frames = 0; frames < 60 && rig.Music().FadeState(0) != kMusicFadeUnset; frames++) {
        rig.Music().FadeTick();
        rig.Music().FadeTick();
        const int level = rig.Step().Level();
        decreasing = decreasing && level <= lastLevel;
        lastLevel = level;
    }
    Check(decreasing, "the level only falls during the fade");
    Check(rig.Music().FadeState(0) == kMusicFadeUnset, "the fade ends");
    Check(rig.Music().TrackGetVolume(0) == 0, "at zero volume");
    rig.Run(1.0);
    Check(rig.Music().SeqState(0) == kSeqStopped, "and the player is stopped");
    Check(rig.Step().Level() == 0, "silent");

    // Fading back in starts the player's sequence over from where it was
    // left only if a sequence is set; here it is played again from the start.
    rig.Music().TrackPlay(0, 1);
    rig.Music().TrackApplySeqpVol(0, 0);
    rig.Run(0.2);
    rig.Music().TrackFadeIn(0, 0.25f, kMusicVolumeMax);
    Check(rig.Music().FadeState(0) == kMusicFadeIn, "fading in");
    for (int frames = 0; frames < 60 && rig.Music().FadeState(0) != kMusicFadeUnset; frames++) {
        rig.Music().FadeTick();
        rig.Music().FadeTick();
        rig.Step();
    }
    Check(rig.Music().TrackGetVolume(0) == kMusicVolumeMax, "the volume is back");
    Check(rig.Run(0.5).back().Level() > 1000, "and the music is audible");
}

void TestSlotsPlayIndependently() {
    Rig rig({ OneNote(2), OneNote(96, true), OneNote(48) });
    rig.Music().TrackPlay(0, 1);
    rig.Music().TrackPlay(2, 2);
    Check(rig.Music().CurrentTrack(0) == 1 && rig.Music().CurrentTrack(1) == 0 && rig.Music().CurrentTrack(2) == 2,
          "each slot has its own track");
    rig.Run(1.5);
    Check(rig.Music().SeqState(0) == kSeqPlaying, "slot 0 still plays its loop");
    Check(rig.Music().SeqState(1) == kSeqStopped, "slot 1 was never started");
    Check(rig.Music().SeqState(2) == kSeqStopped, "slot 2's short track has ended");

    rig.Music().TrackApplySeqpVol(1, 1234);
    Check(rig.Music().TrackGetVolume(1) == 1234 && rig.Music().TrackGetVolume(0) == kMusicVolumeMax,
          "volumes are per slot");
}

void TestPlayWhilePlayingReplacesTheTrack() {
    Rig rig({ OneNote(2), OneNote(96, true), OneNote(48) });
    rig.Music().TrackPlay(0, 1);
    rig.Run(1.0);

    rig.Music().TrackPlay(0, 2);
    Check(rig.Music().CurrentTrack(0) == 2, "the new track number reads back at once");
    // The old track's voices are released first, then the new track loads.
    const std::vector<Peak> peaks = rig.Run(2.0);
    Check(rig.Music().SeqState(0) == kSeqStopped, "the new, shorter track played and ended");
    const double sounded = Rig::SoundedFor(peaks);
    Check(sounded > 0.2 && sounded < 1.5, "audio followed the swap");

    // A play cancelled by a stop before it could start never loads.
    rig.Music().TrackPlay(0, 1);
    rig.Music().TrackPlay(0, 2);
    rig.Music().TrackStop(0);
    rig.Run(1.0);
    Check(rig.Music().SeqState(0) == kSeqStopped && rig.Music().CurrentTrack(0) == 0, "a stop cancels a pending play");
}

void TestTrackWithoutDataFallsBack() {
    // Entry 2 has an address below the ROM's music region once patched.
    Rig rig({ OneNote(2), OneNote(48), OneNote(2) }, 0);
    rig.sequences.entries[2].address = 0;
    rig.engine.reset();
    MusicAssets assets;
    assets.instruments = &rig.instruments;
    assets.sequences = &rig.sequences;
    rig.engine = std::make_unique<AudioEngine>(rig.sfx, EngineConfig(), &assets);

    rig.Music().TrackPlay(0, 2);
    Check(rig.Music().CurrentTrack(0) == kMusicShortSoloDeath, "the short solo death track plays instead");
    Check(Rig::SoundedFor(rig.Run(1.5)) > 0.2, "and it sounds");

    Check(!rig.Music().TrackPlay(0, 99) && !rig.Music().TrackPlay(3, 1) && !rig.Music().TrackPlay(0, -1),
          "a track or slot that does not exist is refused");
}

void TestBootswitchSilencesMusic() {
    Rig rig({ OneNote(2), OneNote(96, true) });
    rig.Music().SetBootswitchSound(true);
    rig.Music().TrackPlay(0, 1);
    Check(rig.Music().CurrentTrack(0) == 0 && rig.Run(0.5).back().Level() == 0,
          "nothing plays while sound is disabled");
}

} // namespace

int main() {
    TestSeqReadsNotesTempoAndEnd();
    TestSeqMergesTracksByTime();
    TestSeqBackReferencesAndEscapedBlockCode();
    TestSeqLoopsTheStatedNumberOfTimes();
    TestTableParses();
    TestTableRejectsDamage();
    TestTrackPlaysAndEnds();
    TestTempoChangeRescalesTime();
    TestLoopedTrackKeepsPlayingUntilStopped();
    TestVolumeScalesOutput();
    TestFadeOutStopsThePlayer();
    TestSlotsPlayIndependently();
    TestPlayWhilePlayingReplacesTheTrack();
    TestTrackWithoutDataFallsBack();
    TestBootswitchSilencesMusic();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::puts("MusicTest passed");
    return 0;
}
