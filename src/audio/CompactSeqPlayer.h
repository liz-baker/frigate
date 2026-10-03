#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "AudioSynth.h"
#include "CompactSeq.h"

// GoldenEye's compact sequence player ("ALCSPlayer", the `alCSP*` calls): a
// queue of timed events that plays a CompactSeq on the synth's voices, one
// MIDI channel state and voice-state pool per player. The music code runs
// three of them (see MusicPlayer).
//
// Calls (Play, Stop, SetSeq, SetBank, SetVol) post events that the player
// handles when the synth next runs it, so their effect follows the next
// AudioFrame, as with the original's audio thread.
namespace Frigate::AudioDriver {

constexpr MicroTime kSeqUsecPerFrame = 16000;
constexpr MicroTime kSeqKillTime = 50000;
constexpr MicroTime kSeqGainChangeTime = 1000;
// Posts an event after every other one.
constexpr MicroTime kEvtqEnd = 0x7fffffff;

constexpr int kSeqMaxChannels = 16;

// Play states (alCSPGetState).
enum SeqPlayState : int32_t {
    kSeqStopped = 0,
    kSeqPlaying = 1,
    kSeqStopping = 2,
};

// Voice phases.
enum SeqPhase : uint8_t {
    kPhaseAttack = 0,
    kPhaseNoteOn = 0,
    kPhaseDecay = 1,
    kPhaseSustain = 2,
    kPhaseRelease = 3,
    kPhaseSustRel = 4,
};

// An instrument bank to take programs from: bank `bank` of `linked`'s file.
struct SeqBank {
    const LinkedBank* linked = nullptr;
    uint32_t bank = 0;
};

struct SeqPlayerConfig {
    int maxVoices = 0;
    int maxEvents = 0;
    int maxChannels = 0;
};

struct VoiceState;

// An event in the player's queue: the sequence's own event fields plus what
// the player's other event types carry.
struct SeqpEvent : SeqEvent {
    VoiceState* voice = nullptr;
    // Envelope events: time to the next volume, and the volume.
    MicroTime delta = 0;
    uint8_t vol = 0;
    int16_t seqpVol = 0;
    uint8_t chan = 0;
    uint8_t priority = 0;
    CompactSeq* seq = nullptr;
    SeqBank bank;
};

// ALVoiceState: a sounding note.
struct VoiceState {
    VoiceState* next = nullptr;
    Voice voice;
    const SoundView* sound = nullptr;
    MicroTime envEndTime = 0;
    float pitch = 0;
    float vibrato = 0;
    uint8_t envGain = 0;
    uint8_t channel = 0;
    uint8_t key = 0;
    uint8_t velocity = 0;
    uint8_t envPhase = 0;
    uint8_t phase = 0;
    uint8_t tremolo = 0;
};

// ALChanState: one MIDI channel's controllers.
struct ChanState {
    uint32_t instrument = Audio::kNoIndex; // index into the bank file's instruments
    int16_t bendRange = 0;
    uint8_t pan = 0;
    uint8_t priority = 0;
    uint8_t vol = 0;
    uint8_t fxMix = 0;
    uint8_t sustain = 0;
    float pitchBend = 1.0f;
};

class CompactSeqPlayer {
  public:
    // alCSPNew. Adds the player to `synth`, which must outlive it.
    CompactSeqPlayer(Synth* synth, const SeqPlayerConfig& config);
    CompactSeqPlayer(const CompactSeqPlayer&) = delete;
    CompactSeqPlayer& operator=(const CompactSeqPlayer&) = delete;

    // alCSPSetSeq: the sequence to play; the player must not be playing.
    void SetSeq(CompactSeq* seq);
    // alSeqpSetBank: the instruments programs select from; the player must
    // be stopped.
    void SetBank(const SeqBank& bank);
    // alCSPPlay.
    void Play();
    // alCSPStop: lets notes release, then stops.
    void Stop();
    // alCSPSetVol: overall volume, 0 to 0x7fff.
    void SetVol(int16_t volume);
    // alCSPGetState.
    int32_t GetState() const {
        return mState;
    }

    int16_t Volume() const {
        return mVol;
    }
    // Notes currently sounding, for tests.
    int ActiveVoices() const;
    int32_t MicrosPerTick() const {
        return mUspt;
    }

  private:
    struct EventItem {
        MicroTime delta = 0;
        SeqpEvent evt;
    };

    // The event queue: ordered by time, each item holding its delay after the
    // one before it.
    void PostEvent(const SeqpEvent& evt, MicroTime delta);
    MicroTime NextEvent(SeqpEvent* evt);
    void FlushType(int16_t type);
    // Reposts an item whose delta is absolute.
    void RepostItem(EventItem item);

    MicroTime VoiceHandler();
    void PostNextSeqEvent();
    void HandleNextSeqEvent();
    void HandleMidiMsg(const SeqpEvent& event);
    void HandleMetaMsg(const SeqpEvent& event);
    void SetUsptFromTempo(float tempo);

    VoiceState* MapVoice(uint8_t key, uint8_t vel, uint8_t channel);
    void UnmapVoice(VoiceState* voice);
    VoiceState* LookupVoice(uint8_t key, uint8_t channel);
    const SoundView* LookupSoundQuick(uint8_t key, uint8_t vel, uint8_t chan) const;
    int16_t VsVol(const VoiceState* vs) const;
    MicroTime VsDelta(const VoiceState* vs, MicroTime t) const;
    uint8_t VsPan(const VoiceState* vs) const;
    void ReleaseVoice(VoiceState* vs, MicroTime deltaTime);
    bool VoiceNeedsNoteKill(const VoiceState* vs, MicroTime killTime);

    void InitFromBank();
    void ResetPerfChanState(int chan);
    void SetInstChanState(uint32_t instrument, int chan);
    const Audio::Instrument* InstrumentAt(uint32_t instrument) const;

    Synth* mSynth;
    Player mPlayer;
    SeqBank mBank;
    CompactSeq* mTarget = nullptr;
    MicroTime mCurTime = 0;
    int32_t mUspt = 488;
    MicroTime mNextDelta = 0;
    int32_t mState = kSeqStopped;
    int16_t mVol = 0x7FFF;
    SeqpEvent mNextEvent;
    MicroTime mFrameTime = kSeqUsecPerFrame;

    std::list<EventItem> mEvents;
    size_t mMaxEvents;

    std::vector<ChanState> mChanState;
    int mMaxChannels;
    std::vector<VoiceState> mVoiceStorage;
    VoiceState* mAllocHead = nullptr;
    VoiceState* mAllocTail = nullptr;
    VoiceState* mFreeList = nullptr;
};

} // namespace Frigate::AudioDriver
