#pragma once

#include <cstddef>
#include <cstdint>

// GoldenEye's compact MIDI sequence ("ALCSeq"): up to 16 tracks of
// delta-timed events in a compact encoding with back-reference blocks and
// loop markers. The sequence reads from, and writes loop counters back into,
// the buffer it is created over, so that buffer must outlive the sequence and
// belong to one player.
//
// Sequence data layout: docs/formats/goldeneye-music-sequences.md.
namespace Frigate::AudioDriver {

// Event types shared by the sequence and its player (libultra's `ALMsg`).
enum SeqMsg : int16_t {
    kSeqNoEvent = -1,
    kSeqRefEvt = 0,
    kSeqMidiEvt,
    kSeqpMidiEvt,
    kTempoEvt,
    kSeqEndEvt,
    kNoteEndEvt,
    kSeqpEnvEvt,
    kSeqpMetaEvt,
    kSeqpProgEvt,
    kSeqpApiEvt,
    kSeqpVolEvt,
    kSeqpLoopEvt,
    kSeqpPriorityEvt,
    kSeqpSeqEvt,
    kSeqpBankEvt,
    kSeqpPlayEvt,
    kSeqpStopEvt,
    kSeqpStoppingEvt,
    kTrackEnd,
    kCspLoopStart,
    kCspLoopEnd,
    kCspNoteOffEvt,
};

enum MidiStatus : uint8_t {
    kMidiChannelMask = 0x0F,
    kMidiStatusMask = 0xF0,
    kMidiNoteOff = 0x80,
    kMidiNoteOn = 0x90,
    kMidiPolyKeyPressure = 0xA0,
    kMidiControlChange = 0xB0,
    kMidiProgramChange = 0xC0,
    kMidiChannelPressure = 0xD0,
    kMidiPitchBendChange = 0xE0,
    kMidiMeta = 0xFF,
};

constexpr uint8_t kMidiMetaTempo = 0x51;
constexpr uint8_t kMidiMetaEndOfTrack = 0x2F;
constexpr uint8_t kCmidiBlockCode = 0xFE;
constexpr uint8_t kCmidiLoopStartCode = 0x2E;
constexpr uint8_t kCmidiLoopEndCode = 0x2D;

constexpr int kSeqTrackCount = 16;

// An event as the sequence fills it in.
struct SeqEvent {
    int16_t type = kSeqNoEvent;
    int32_t ticks = 0;
    struct {
        uint8_t status = 0;
        uint8_t byte1 = 0;
        uint8_t byte2 = 0;
        uint32_t duration = 0;
    } midi;
    struct {
        uint8_t status = 0;
        uint8_t type = 0;
        uint8_t byte1 = 0;
        uint8_t byte2 = 0;
        uint8_t byte3 = 0;
    } tempo;
};

class CompactSeq {
  public:
    // alCSeqNew: starts a sequence over `data` (`size` bytes).
    void New(uint8_t* data, size_t size);

    // alCSeqNextEvent: reads the next event, from whichever track has the
    // soonest one.
    void NextEvent(SeqEvent* event);

    // __alCSeqNextDelta: ticks until the next event; false when every track
    // has ended.
    bool NextDelta(int32_t* deltaTicks);

    // Quarter notes per tick.
    float Qnpt() const {
        return mQnpt;
    }

  private:
    uint8_t ByteAt(uint32_t position) const;
    uint32_t ReadVarLen(int track);
    uint8_t GetTrackByte(int track);
    void GetTrackEvent(int track, SeqEvent* event);

    // Positions are byte offsets into the data; 0 is "none", as no track or
    // back-reference can start inside the header.
    uint8_t* mData = nullptr;
    size_t mSize = 0;
    uint32_t mValidTracks = 0;
    float mQnpt = 0;
    uint32_t mLastTicks = 0;
    uint32_t mLastDeltaTicks = 0;
    uint32_t mDeltaFlag = 0;
    uint32_t mCurLoc[kSeqTrackCount] = {};
    uint32_t mCurBackupPtr[kSeqTrackCount] = {};
    uint8_t mCurBackupLen[kSeqTrackCount] = {};
    uint8_t mLastStatus[kSeqTrackCount] = {};
    uint32_t mEvtDeltaTicks[kSeqTrackCount] = {};
};

} // namespace Frigate::AudioDriver
