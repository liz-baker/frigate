#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <vector>

#include "AudioBank.h"
#include "AudioUcode.h"

// The audio synthesis driver: GoldenEye's libultra voice pool, run against
// AudioUcode. The game builds a command list for the RSP's audio microcode
// each frame; here every command executes as it is generated, which is the
// same order the RSP would have run them in. Addresses the commands take in
// DRAM are host pointers.
//
// Voices form a fixed chain, decoder -> resampler -> envelope mixer, mixed
// onto a main bus and an effects (reverb) bus, then saved as interleaved PCM.
namespace Frigate::AudioDriver {

// Longest span of samples one pass of the chain produces.
constexpr int kMaxRspSamples = 160;

// DMEM layout, in bytes, for a pass of up to kMaxRspSamples.
constexpr uint16_t kDecoderIn = 0;
constexpr uint16_t kResamplerOut = 0;
constexpr uint16_t kTemp0 = 0;
constexpr uint16_t kDecoderOut = 320;
constexpr uint16_t kTemp1 = 320;
constexpr uint16_t kTemp2 = 640;
constexpr uint16_t kMainLOut = 1088;
constexpr uint16_t kMainROut = 1408;
constexpr uint16_t kAuxLOut = 1728;
constexpr uint16_t kAuxROut = 2048;

// Microseconds, as the libultra audio API counts time.
using MicroTime = int32_t;

constexpr int kPanLeft = 0;
constexpr int kPanCenter = 64;
constexpr int kPanRight = 127;

// Voice states.
constexpr int16_t kStopped = 0;
constexpr int16_t kPlaying = 1;
constexpr int16_t kStopping = 2;

// A wavetable bound to its sample data.
struct Wave {
    const uint8_t* samples = nullptr; // start of the owning bank's sample table
    uint32_t base = 0;                // byte offset of this wave's data
    int32_t length = 0;
    uint8_t type = Audio::kAdpcmWave;
    const Audio::Loop* loop = nullptr;
    const Audio::Book* book = nullptr;
};

// A bank's sample table copied to 8-byte-aligned memory with slack on both
// sides: the loader DMAs in aligned blocks that start before and run past a
// wave's own bytes.
class SampleTable {
  public:
    void Assign(const std::vector<uint8_t>& samples);
    const uint8_t* Data() const {
        return mData;
    }
    size_t Size() const {
        return mSize;
    }

  private:
    static constexpr size_t kSlack = 32;
    std::vector<uint8_t> mStorage;
    const uint8_t* mData = nullptr;
    size_t mSize = 0;
};

// A bank with its waves and sounds resolved from indices to the structures
// the driver reads. Must outlive anything that plays from it.
struct SoundView {
    const Audio::Envelope* envelope = nullptr;
    const Audio::KeyMap* keyMap = nullptr;
    const Wave* wave = nullptr;
    uint8_t samplePan = 0;
    uint8_t sampleVolume = 0;
};

class LinkedBank {
  public:
    explicit LinkedBank(const Audio::AudioBank& bank);
    LinkedBank(const LinkedBank&) = delete;
    LinkedBank& operator=(const LinkedBank&) = delete;

    const Audio::AudioBank& Bank() const {
        return *mBank;
    }
    // The sound at `index` of instrument `instrument`, or null.
    const SoundView* FindSound(uint32_t instrument, size_t index) const;

  private:
    const Audio::AudioBank* mBank;
    SampleTable mSamples;
    std::vector<Wave> mWaves;
    std::vector<SoundView> mSounds;
};

// libultra's effects presets, plus the game's own section table.
enum FxType {
    kFxNone,
    kFxSmallRoom,
    kFxBigRoom,
    kFxEcho,
    kFxChorus,
    kFxFlange,
    kFxCustom,
};

struct SynConfig {
    int maxPVoices = 0;
    int maxUpdates = 0;
    int outputRate = 0;
    FxType fxType = kFxNone;
    // For kFxCustom: section count, delay length, then 8 words per section.
    std::vector<int32_t> fxParams;
};

// Messages carried by parameter updates and passed down a voice's chain.
enum ParamId {
    kFreeVoice,
    kAddUpdate,
    kReset,
    kSetWavetable,
    kSetPitch,
    kSetUnityPitch,
    kStart,
    kSetVolume,
    kSetPan,
    kStartVoiceAlt,
    kStartVoice,
    kStopVoice,
    kSetFxAmount,
};

struct PVoice;

// A queued change to a physical voice, applied when the sample clock reaches
// `delta`.
struct Param {
    Param* next = nullptr;
    int32_t delta = 0;
    int16_t type = 0;
    int32_t dataInt = 0;
    float dataFloat = 0;
    int32_t moreInt = 0;
    int16_t unity = 0;
    float pitch = 0;
    int16_t volume = 0;
    uint8_t pan = 0;
    uint8_t fxMix = 0;
    int32_t samples = 0;
    const Wave* wave = nullptr;
    PVoice* pvoice = nullptr;
};

// A virtual voice: what a player holds while a sound plays.
struct Voice {
    PVoice* pvoice = nullptr;
    const Wave* table = nullptr;
    int16_t state = kStopped;
    int16_t priority = 0;
    int16_t fxBus = 0;
    int16_t unityPitch = 0;
};

struct VoiceConfig {
    int16_t priority = 0;
    int16_t fxBus = 0;
    uint8_t unityPitch = 0;
};

// A client of the synth: called back when its time comes, and returns how
// long until the next call.
struct Player {
    std::function<MicroTime()> handler;
    int32_t samplesLeft = 0;
};

class Synth;

// The decoder filter: turns a wave's ADPCM or raw data into 16-bit samples
// in DMEM.
class LoadFilter {
  public:
    // Runs the decode for `outCount` samples; `outp` is the DMEM address of
    // the decoded samples on return.
    void Pull(uint16_t& outp, int outCount);
    void SetParam(int paramId, const Wave* wave);

  private:
    void PullAdpcm(uint16_t& outp, int outCount);
    void PullRaw16(uint16_t& outp, int outCount);
    void DecodeChunk(int tsam, int nbytes, uint16_t outp, uint16_t inp, uint32_t flags);
    const uint8_t* Dma(int32_t offset, int32_t length) const;

    const Wave* mTable = nullptr;
    int32_t mTableLength = 0;
    int32_t mBookSize = 0;
    struct {
        uint32_t start = 0;
        uint32_t end = 0;
        uint32_t count = 0;
    } mLoop;
    AudioUcode::AdpcmState mState{};
    AudioUcode::AdpcmState mLoopState{};
    int32_t mSample = 0;
    int32_t mLastSample = 0;
    int32_t mFirst = 1;
    int32_t mMemIn = 0;
};

class Resampler {
  public:
    explicit Resampler(LoadFilter* source) : mSource(source) {
    }
    void Pull(uint16_t& outp, int outCount, int sampleOffset);
    void SetParam(int paramId, const Wave* wave, float floatValue);

  private:
    LoadFilter* mSource;
    AudioUcode::ResampleState mState{};
    float mRatio = 1.0f;
    int32_t mUnityPitch = 0;
    float mDelta = 0.0f;
    int32_t mFirst = 1;
    int32_t mMotion = kStopped;
};

class EnvMixer {
  public:
    EnvMixer(Synth* synth, Resampler* source) : mSynth(synth), mSource(source) {
    }
    void Pull(uint16_t& outp, int outCount, int sampleOffset);
    void SetParam(int paramId, Param* update, const Wave* wave, float floatValue);
    void AddUpdate(Param* update);

  private:
    void PullSubFrame(uint16_t& inp, uint16_t outp, int outCount, int sampleOffset);
    void Apply(Param* update);

    Synth* mSynth;
    Resampler* mSource;
    AudioUcode::EnvMixerState mState{};
    int16_t mPan = 0;
    int16_t mVolume = 1;
    int16_t mCvolL = 1;
    int16_t mCvolR = 1;
    int16_t mDryAmt = 0;
    int16_t mWetAmt = 0;
    uint16_t mLratl = 0;
    int16_t mLratm = 1;
    int16_t mLtgt = 1;
    uint16_t mRratl = 0;
    int16_t mRratm = 0;
    int16_t mRtgt = 1;
    int32_t mDelta = 0;
    int32_t mSegEnd = 0;
    int32_t mFirst = 1;
    Param* mCtrlList = nullptr;
    Param* mCtrlTail = nullptr;
    int32_t mMotion = kStopped;

    friend class Synth;
};

// A physical voice.
struct PVoice {
    PVoice(Synth* synth) : resampler(&decoder), envmixer(synth, &resampler) {
    }

    Voice* vvoice = nullptr;
    LoadFilter decoder;
    Resampler resampler;
    EnvMixer envmixer;
    int32_t offset = 0;

    // Sends a parameter message to the head of the chain (the envelope mixer).
    void SetParam(int paramId, Param* update = nullptr, const Wave* wave = nullptr, float floatValue = 0.0f);
};

// One delay line section of the reverb, and its optional chorus and filter.
struct FxDelay {
    uint32_t input = 0;
    uint32_t output = 0;
    int16_t ffcoef = 0;
    int16_t fbcoef = 0;
    int16_t gain = 0;
    float rsinc = 0;
    float rsval = 0;
    int32_t rsdelta = 0;
    float rsgain = 0;
    bool hasLowPass = false;
    int16_t lpCoefficient = 0;
    int16_t lpGain = 0;
    int16_t lpVector[16] = {};
    bool lpFirst = true;
    int16_t lpState[16] = {};
    bool hasResampler = false;
    AudioUcode::ResampleState rsState{};
    float rsDelta = 0;
    int32_t rsFirst = 1;
};

class Fx {
  public:
    explicit Fx(const SynConfig& config);
    // Mixes the effect over the aux bus in DMEM, which the voices have
    // already filled.
    void Pull(int outCount);

  private:
    void LoadOutputBuffer(FxDelay& d, uint16_t buff, int incount);
    void LoadBuffer(int64_t pos, uint16_t buff, int count);
    void SaveBuffer(int64_t pos, uint16_t buff, int count);
    void FilterBuffer(FxDelay& d, uint16_t buff, int count);
    float DoModFunc(FxDelay& d, int count);

    std::vector<FxDelay> mDelays;
    std::vector<int16_t> mBaseStorage;
    int16_t* mBase = nullptr;
    // Positions in the delay line are samples from mBase; one before the
    // start wraps once.
    int64_t mInputPos = 0;
    uint32_t mLength = 0;
};

// The synthesis driver. Players post voice changes through the Alloc/Start/Set
// calls; AudioFrame runs the players due this frame and then builds the
// frame's samples.
class Synth {
  public:
    explicit Synth(const SynConfig& config);
    ~Synth();
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    void AddPlayer(Player* player);

    // Fills `out` with `outLen` interleaved stereo frames (`outLen * 2`
    // samples). `outLen` is a multiple of 16.
    void AudioFrame(int16_t* out, int outLen);

    bool AllocVoice(Voice* voice, const VoiceConfig& config);
    void FreeVoice(Voice* voice);
    void StartVoice(Voice* voice, const Wave* table);
    void StartVoiceParams(Voice* voice, const Wave* wave, float pitch, int16_t volume, uint8_t pan, uint8_t fxMix,
                          MicroTime time);
    void StopVoice(Voice* voice);
    void SetVolume(Voice* voice, int16_t volume, MicroTime time);
    void SetPitch(Voice* voice, float pitch);
    void SetPan(Voice* voice, uint8_t pan);
    void SetFxMix(Voice* voice, uint8_t fxMix);

    int32_t TimeToSamples(MicroTime micros) const;
    int32_t TimeToSamplesNoRound(MicroTime micros) const;
    int32_t CurrentSamples() const {
        return mCurSamples;
    }
    int OutputRate() const {
        return mOutputRate;
    }

    Param* AllocParam();
    void FreeParam(Param* param);
    void FreePVoice(PVoice* pvoice);

  private:
    int32_t NextSampleTime(Player** client);
    void CollectPVoices();
    PVoice* AllocatePVoice(int16_t priority, bool* stolen);
    void PullChain(int16_t* out, int nOut);

    std::vector<std::unique_ptr<PVoice>> mVoiceStorage;
    std::vector<Param> mParamStorage;
    Param* mParamList = nullptr;
    std::deque<PVoice*> mFreeList;
    std::deque<PVoice*> mAllocList;
    std::deque<PVoice*> mLameList;
    std::vector<Player*> mPlayers;
    std::unique_ptr<Fx> mFx;
    int32_t mParamSamples = 0;
    int32_t mCurSamples = 0;
    int mOutputRate = 0;

    friend class EnvMixer;
};

// 2^(cents / 1200), by repeated squaring so it matches the original's
// rounding.
float CentsToRatio(int32_t cents);

} // namespace Frigate::AudioDriver
