#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "AudioSynth.h"

// GoldenEye's sound-effect player: a queue of timed events that start, shape
// and stop sounds from a bank on the synth's voices, one SoundState per
// sound. Sounds are named by their index in the sound-effect bank's first
// instrument, counted from one (index 0 is "no sound").
namespace Frigate::AudioDriver {

constexpr int kSfxSlotCount = 7;

constexpr MicroTime kDelta1Ms = 1000;
constexpr MicroTime kDelta33Ms = 33333;
constexpr MicroTime kUsecPerFrame30Fps = 33000;

constexpr int kSndpGroupVolumeMax = 32767;

// Flags in SoundState::flags.
enum SoundFlags : uint8_t {
    kSoundFinalInSequence = 1 << 0,
    kSoundLooped = 1 << 1,
    kSoundPlaying = 1 << 2,
    kSoundRetrigger = 1 << 4,
    kSoundPitchSlide = 1 << 5,
};

enum SoundStateId : uint8_t {
    kSoundStateNone = 0,
    kSoundStatePlaying = 1,
    kSoundStateStopping = 2,
    kSoundStatePreempt = 3,
    kSoundStateWaitVoice = 4,
    kSoundStateInit = 5,
};

// Event types; a bit each, so a removal can name several at once.
enum SndpEvent : uint16_t {
    kSndpPlay = 1 << 0,
    kSndpStop = 1 << 1,
    kSndpPan = 1 << 2,
    kSndpVol = 1 << 3,
    kSndpPitch = 1 << 4,
    kSndpApi = 1 << 5,
    kSndpDecay = 1 << 6,
    kSndpEnd = 1 << 7,
    kSndpFx = 1 << 8,
    kSndpPlaySfx = 1 << 9,
    kSndpDeactivate = 1 << 10,
    kSndpRelease = 1 << 11,
    kSndpUnknown12 = 1 << 12,
};

struct SoundState;

struct SndEvent {
    // -1 marks "no event".
    int32_t type = -1;
    SoundState* state = nullptr;
    // The one argument events carry: a volume, pan or mix, or a pitch ratio.
    union {
        int32_t i = 0;
        float f;
    } arg;
    // For kSndpPlaySfx.
    int32_t soundIndex = 0;
    const LinkedBank* bank = nullptr;
};

// ALSoundState.
struct SoundState {
    SoundState* next = nullptr;
    SoundState* prev = nullptr;
    const SoundView* sound = nullptr;
    Voice voice;
    float pitchBase = 1.0f; // from the sound's key map
    float pitchBend = 1.0f; // set by pitch events
    // The caller's handle for this sound (PlaySfx's `pending`): cleared when
    // the sound is disposed of, if it still names it.
    SoundState** chained = nullptr;
    int16_t vol = 0;
    uint8_t priority = 0;
    int32_t retries = 0;
    uint8_t pan = 0;
    uint8_t fxMix = 0;
    uint8_t flags = 0;
    uint8_t playingState = kSoundStateNone;
};

struct SoundPlayerConfig {
    int stateCount = 0x40;
    int maxEvents = 0x40;
    int maxSounds = 8;
};

class SoundPlayer {
  public:
    SoundPlayer(Synth* synth, const SoundPlayerConfig& config);
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;

    // Starts the sound `soundIndex` of `bank` (and any sounds chained after
    // it). `pending`, when given, is the caller's handle: it is set to the new
    // state, and reset to null when that state ends. Returns the new state, or
    // null if sound is disabled or none was free.
    SoundState* PlaySfx(const LinkedBank& bank, int16_t soundIndex, SoundState** pending);
    void Deactivate(SoundState* state);
    // Forgets `handle` (a PlaySfx `pending`) for any sound still holding it, for
    // a handle about to go out of scope while its sound plays on.
    void DetachHandle(SoundState** handle);
    void DeactivateAllByFlag(uint8_t flag);
    void CreatePostEvent(SoundState* state, int16_t eventType, int32_t arg);

    uint8_t GetPlayingState(const SoundState* state) const;
    void SetPriority(SoundState* state, uint8_t priority);

    uint16_t GetSfxSlotNaturalVolume(uint8_t slot) const {
        return mSlotNaturalVolume[slot];
    }
    uint16_t GetSfxSlotFirstNaturalVolume() const {
        return GetSfxSlotNaturalVolume(0);
    }
    void SetSfxSlotVolume(uint8_t slot, uint16_t volume);
    void ApplyVolumeAllSfxSlot(uint16_t volume);
    void SetScalerApplyVolumeAllSfxSlot(float scale);

    // Sound is silently disabled when set.
    void SetBootswitchSound(bool disabled) {
        mBootswitchSound = disabled;
    }

    int AllocatedVoices() const {
        return mAllocatedVoices;
    }
    // Sound states in use, for tests.
    int ActiveStates() const;

  private:
    struct EventItem {
        int32_t delta = 0;
        SndEvent evt;
    };

    // Event queue.
    void PostEvent(const SndEvent& evt, MicroTime delta);
    MicroTime NextEvent(SndEvent* evt);
    void RemoveEvents(SoundState* state, uint16_t eventMask);

    MicroTime VoiceHandler();
    void HandleEvent(SndEvent* event);
    void DisposeSound(SoundState* state);
    void CreatePitchEvent(SoundState* state);
    SoundState* SetupSound(const SoundView* sound);
    void UnlinkClearSound(SoundState* state);

    Synth* mSynth;
    Player mPlayer;
    std::vector<SoundState> mStates;
    // Active states, newest first; and the free list.
    SoundState* mActiveHead = nullptr;
    SoundState* mActiveTail = nullptr;
    SoundState* mFreeHead = nullptr;

    std::list<EventItem> mEvents;
    size_t mMaxEvents;

    int mMaxSounds;
    MicroTime mFrameTime = kUsecPerFrame30Fps;
    MicroTime mNextDelta = 0;
    int32_t mCurTime = 0;
    SndEvent mNextEvent;

    int mAllocatedVoices = 0;
    bool mBootswitchSound = false;
    float mSfxVolumeScale = 1.0f;
    int16_t mSlotVolume[kSfxSlotCount];
    uint16_t mSlotNaturalVolume[kSfxSlotCount];
};

} // namespace Frigate::AudioDriver
