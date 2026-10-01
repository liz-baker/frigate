#include "AudioSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Frigate::AudioDriver {

namespace {

constexpr int kAdpcmFrameSamples = 16;
constexpr int kAdpcmFrameBytes = 9;
constexpr int kAdpcmFrameShift = 4;
constexpr int kAdpcmVectorSize = 8;

// The resampler's fixed-point unity ratio, and the largest ratio it accepts
// (within 0.03 cents of one octave up).
constexpr int kUnityPitch = 0x8000;
constexpr float kMaxRatio = 1.99996f;

constexpr int kEqPowerLength = 128;

// Equal-power pan/mix curve: cos over a quarter period, 128 steps.
constexpr int16_t kEqPower[kEqPowerLength] = {
    32767, 32764, 32757, 32744, 32727, 32704, 32677, 32644, 32607, 32564, 32517, 32464, 32407, 32344, 32277, 32205,
    32127, 32045, 31958, 31866, 31770, 31668, 31561, 31450, 31334, 31213, 31087, 30957, 30822, 30682, 30537, 30388,
    30234, 30075, 29912, 29744, 29572, 29395, 29214, 29028, 28838, 28643, 28444, 28241, 28033, 27821, 27605, 27385,
    27160, 26931, 26698, 26461, 26220, 25975, 25726, 25473, 25216, 24956, 24691, 24423, 24151, 23875, 23596, 23313,
    23026, 22736, 22442, 22145, 21845, 21541, 21234, 20924, 20610, 20294, 19974, 19651, 19325, 18997, 18665, 18331,
    17993, 17653, 17310, 16965, 16617, 16266, 15913, 15558, 15200, 14840, 14477, 14113, 13746, 13377, 13006, 12633,
    12258, 11881, 11503, 11122, 10740, 10357, 9971,  9584,  9196,  8806,  8415,  8023,  7630,  7235,  6839,  6442,
    6044,  5646,  5246,  4845,  4444,  4042,  3640,  3237,  2833,  2429,  2025,  1620,  1216,  810,   405,   0,
};

// Reverb constants.
constexpr double kChorusRange = 2.0;
constexpr double kChorusConvert = 173123.404906676; // 120000 / ln 2, hundredths of a cent
constexpr int kLowPassScale = 16384;

namespace Ucode = Frigate::AudioUcode;

// Gain and rate of a volume ramp from `vol` to `tgt` over `count` samples,
// as the ucode's high and low rate words.
int16_t GetRate(float vol, float tgt, int32_t count, uint16_t* ratel) {
    if (count == 0) {
        if (tgt >= vol) {
            *ratel = 0xffff;
            return 0x7fff;
        }
        *ratel = 0;
        return -0x8000;
    }

    float tempf2 = ((tgt - vol) / count) * 8.0f;
    if (tempf2 < 0.0f) {
        tempf2 -= 1.0f;
    }
    const int16_t s = static_cast<int16_t>(tempf2);
    *ratel = static_cast<uint16_t>(static_cast<int16_t>(0xffff * (tempf2 - static_cast<float>(s))));
    return static_cast<int16_t>(tempf2);
}

float GetVol(float ivol, int32_t samples, int16_t ratem, uint16_t ratel) {
    const float rate = (static_cast<float>(ratem << 16) + static_cast<float>(ratel)) / 65536.0f;
    ivol += (rate * static_cast<float>(samples)) * 0.125f;
    return ivol;
}

} // namespace

float CentsToRatio(int32_t cents) {
    float ratio = 1.0f;
    float x;
    if (cents >= 0) {
        x = 1.00057779f;
    } else {
        x = 0.9994225441f;
        cents = -cents;
    }
    while (cents) {
        if (cents & 1) {
            ratio *= x;
        }
        x *= x;
        cents >>= 1;
    }
    return ratio;
}

// ---------------------------------------------------------------------------
// Sample tables and linked banks

void SampleTable::Assign(const std::vector<uint8_t>& samples) {
    mStorage.assign(samples.size() + 2 * kSlack + 8, 0);
    // Align the data to 8 bytes so DRAM alignment inside a wave matches its
    // offset in the table.
    const uintptr_t start = reinterpret_cast<uintptr_t>(mStorage.data()) + kSlack;
    const uintptr_t aligned = (start + 7) & ~uintptr_t(7);
    uint8_t* data = reinterpret_cast<uint8_t*>(aligned);
    std::memcpy(data, samples.data(), samples.size());
    mData = data;
    mSize = samples.size();
}

LinkedBank::LinkedBank(const Audio::AudioBank& bank) : mBank(&bank) {
    mSamples.Assign(bank.samples);

    // Raw 16-bit waves are stored big-endian; the ucode reads host order.
    std::vector<bool> swapped((bank.samples.size() + 1) / 2, false);

    mWaves.resize(bank.wavetables.size());
    for (size_t i = 0; i < bank.wavetables.size(); i++) {
        const Audio::WaveTable& source = bank.wavetables[i];
        Wave& wave = mWaves[i];
        wave.samples = mSamples.Data();
        wave.base = source.base;
        wave.length = static_cast<int32_t>(source.length);
        wave.type = source.type;
        wave.loop = source.loop == Audio::kNoIndex ? nullptr : &bank.loops[source.loop];
        wave.book = source.book == Audio::kNoIndex ? nullptr : &bank.books[source.book];

        if (source.type == Audio::kRaw16Wave) {
            uint8_t* data = const_cast<uint8_t*>(mSamples.Data());
            for (uint32_t b = source.base & ~1u; b + 1 < source.base + source.length; b += 2) {
                if (!swapped[b / 2]) {
                    swapped[b / 2] = true;
                    std::swap(data[b], data[b + 1]);
                }
            }
        }
    }

    mSounds.resize(bank.sounds.size());
    for (size_t i = 0; i < bank.sounds.size(); i++) {
        const Audio::Sound& source = bank.sounds[i];
        SoundView& view = mSounds[i];
        view.envelope = &bank.envelopes[source.envelope];
        view.keyMap = &bank.keymaps[source.keymap];
        view.wave = &mWaves[source.wavetable];
        view.samplePan = source.samplePan;
        view.sampleVolume = source.sampleVolume;
    }
}

const SoundView* LinkedBank::FindSound(uint32_t instrument, size_t index) const {
    if (instrument >= mBank->instruments.size()) {
        return nullptr;
    }
    const Audio::Instrument& inst = mBank->instruments[instrument];
    if (index >= inst.sounds.size()) {
        return nullptr;
    }
    return &mSounds[inst.sounds[index]];
}

// ---------------------------------------------------------------------------
// Decoder

const uint8_t* LoadFilter::Dma(int32_t offset, int32_t length) const {
    (void)length;
    return mTable->samples + offset;
}

void LoadFilter::SetParam(int paramId, const Wave* wave) {
    switch (paramId) {
        case kSetWavetable:
            mTable = wave;
            mMemIn = static_cast<int32_t>(wave->base);
            mSample = 0;
            switch (wave->type) {
                case Audio::kAdpcmWave:
                    // The table is an integer number of frames.
                    mTableLength = kAdpcmFrameBytes * (wave->length / kAdpcmFrameBytes);
                    mBookSize = 2 * wave->book->order * wave->book->npredictors * kAdpcmVectorSize;
                    if (wave->loop != nullptr) {
                        mLoop.start = wave->loop->start;
                        mLoop.end = wave->loop->end;
                        mLoop.count = wave->loop->count;
                        std::memcpy(mLoopState.words, wave->loop->state, sizeof(mLoopState.words));
                    } else {
                        mLoop.start = mLoop.end = mLoop.count = 0;
                    }
                    break;
                case Audio::kRaw16Wave:
                    mTableLength = wave->length;
                    if (wave->loop != nullptr) {
                        mLoop.start = wave->loop->start;
                        mLoop.end = wave->loop->end;
                        mLoop.count = wave->loop->count;
                    } else {
                        mLoop.start = mLoop.end = mLoop.count = 0;
                    }
                    break;
                default:
                    break;
            }
            break;
        case kReset:
            mLastSample = 0;
            mFirst = 1;
            mSample = 0;
            if (mTable != nullptr) {
                mMemIn = static_cast<int32_t>(mTable->base);
                if (mTable->loop != nullptr) {
                    mLoop.count = mTable->loop->count;
                }
            }
            break;
        default:
            break;
    }
}

void LoadFilter::Pull(uint16_t& outp, int outCount) {
    if (mTable == nullptr) {
        return;
    }
    if (mTable->type == Audio::kRaw16Wave) {
        PullRaw16(outp, outCount);
    } else {
        PullAdpcm(outp, outCount);
    }
}

void LoadFilter::DecodeChunk(int tsam, int nbytes, uint16_t outp, uint16_t inp, uint32_t flags) {
    int dramAlign = 0;
    if (nbytes > 0) {
        const uint8_t* dramLoc = Dma(mMemIn, nbytes);
        dramAlign = static_cast<int>(reinterpret_cast<uintptr_t>(dramLoc) & 7);
        nbytes += dramAlign;
        Ucode::SetBuffer(inp, 0, static_cast<uint16_t>(nbytes + 8 - (nbytes & 7)));
        Ucode::LoadBuffer(dramLoc - dramAlign);
    }

    if (flags & Ucode::kAdpcmLoop) {
        Ucode::SetLoopState(&mLoopState);
    }

    Ucode::SetBuffer(static_cast<uint16_t>(inp + dramAlign), outp, static_cast<uint16_t>(tsam << 1));
    Ucode::AdpcmDecode(flags, &mState);
    mFirst = 0;
}

void LoadFilter::PullAdpcm(uint16_t& outp, int outCount) {
    if (outCount == 0) {
        return;
    }

    const uint16_t inp = kDecoderIn;
    Ucode::LoadAdpcmBook(mTable->book->words.data(), static_cast<uint32_t>(mBookSize));

    int32_t nSam;
    const bool looped = static_cast<uint32_t>(outCount + mSample) > mLoop.end && mLoop.count != 0;
    if (looped) {
        nSam = static_cast<int32_t>(mLoop.end - static_cast<uint32_t>(mSample));
    } else {
        nSam = outCount;
    }

    int32_t nLeft = mLastSample ? kAdpcmFrameSamples - mLastSample : 0;
    int32_t tsam = nSam - nLeft;
    if (tsam < 0) {
        tsam = 0;
    }

    int32_t nframes = (tsam + kAdpcmFrameSamples - 1) >> kAdpcmFrameShift;
    int32_t nbytes = nframes * kAdpcmFrameBytes;

    if (looped) {
        DecodeChunk(tsam, nbytes, outp, inp, mFirst);

        // The next stage reads from the end of the lead-in.
        if (mLastSample) {
            outp = static_cast<uint16_t>(outp + (mLastSample << 1));
        } else {
            outp = static_cast<uint16_t>(outp + (kAdpcmFrameSamples << 1));
        }

        // Continue from the loop start.
        mLastSample = static_cast<int32_t>(mLoop.start & 0xf);
        mMemIn = static_cast<int32_t>(mTable->base) +
                 kAdpcmFrameBytes * static_cast<int32_t>((mLoop.start >> kAdpcmFrameShift) + 1);
        mSample = static_cast<int32_t>(mLoop.start);

        int32_t bEnd = outp;
        while (outCount > nSam) {
            outCount -= nSam;

            // The next chunk starts on the 32-byte boundary after the last.
            const int32_t op = (bEnd + ((nframes + 1) << (kAdpcmFrameShift + 1))) & ~0x1f;

            bEnd += nSam << 1;

            // A count of -1 loops forever.
            if (mLoop.count != 0xFFFFFFFFu && mLoop.count != 0) {
                mLoop.count--;
            }

            nSam = std::min<int32_t>(outCount, static_cast<int32_t>(mLoop.end - mLoop.start));
            tsam = nSam - kAdpcmFrameSamples + mLastSample;
            if (tsam < 0) {
                tsam = 0;
            }
            nframes = (tsam + kAdpcmFrameSamples - 1) >> kAdpcmFrameShift;
            nbytes = nframes * kAdpcmFrameBytes;
            DecodeChunk(tsam, nbytes, static_cast<uint16_t>(op), inp, mFirst | Ucode::kAdpcmLoop);
            // Splice the two sections together in DMEM.
            Ucode::DmemMove(static_cast<uint16_t>(op + (mLastSample << 1)), static_cast<uint16_t>(bEnd),
                            static_cast<uint32_t>(nSam << 1));
        }

        mLastSample = (outCount + mLastSample) & 0xf;
        mSample += outCount;
        mMemIn += kAdpcmFrameBytes * nframes;
        return;
    }

    // Not looping: the common case.
    nSam = nframes << kAdpcmFrameShift;

    // Bytes the chunk would read past the end of the data.
    int32_t overFlow = mMemIn + nbytes - (static_cast<int32_t>(mTable->base) + mTableLength);
    if (overFlow < 0) {
        overFlow = 0;
    }
    int32_t nOver = (overFlow / kAdpcmFrameBytes) << kAdpcmFrameShift;
    if (nOver > nSam + nLeft) {
        nOver = nSam + nLeft;
    }

    nbytes -= overFlow;

    bool decoded = false;
    if ((nOver - (nOver & 0xf)) < outCount) {
        decoded = true;
        DecodeChunk(nSam - nOver, nbytes, outp, inp, mFirst);

        if (mLastSample) {
            outp = static_cast<uint16_t>(outp + (mLastSample << 1));
        } else {
            outp = static_cast<uint16_t>(outp + (kAdpcmFrameSamples << 1));
        }

        mLastSample = (outCount + mLastSample) & 0xf;
        mSample += outCount;
        mMemIn += kAdpcmFrameBytes * nframes;
    } else {
        mLastSample = 0;
        mMemIn += kAdpcmFrameBytes * nframes;
    }

    // Silence past the end of the data.
    if (nOver) {
        mLastSample = 0;
        const int32_t startZero = decoded ? (nLeft + nSam - nOver) << 1 : 0;
        Ucode::ClearBuffer(static_cast<uint16_t>(startZero + outp), static_cast<uint32_t>(nOver << 1));
    }
}

void LoadFilter::PullRaw16(uint16_t& outp, int outCount) {
    if (outCount == 0) {
        return;
    }

    if (static_cast<uint32_t>(outCount + mSample) > mLoop.end && mLoop.count != 0) {
        int32_t nSam = static_cast<int32_t>(mLoop.end - static_cast<uint32_t>(mSample));
        int32_t nbytes = nSam << 1;
        int dramAlign = 0;
        if (nSam > 0) {
            const uint8_t* dramLoc = Dma(mMemIn, nbytes);
            // Load enough to cover the 8-byte alignment.
            dramAlign = static_cast<int>(reinterpret_cast<uintptr_t>(dramLoc) & 7);
            nbytes += dramAlign;
            Ucode::SetBuffer(outp, 0, static_cast<uint16_t>(nbytes + 8 - (nbytes & 7)));
            Ucode::LoadBuffer(dramLoc - dramAlign);
        }

        outp = static_cast<uint16_t>(outp + dramAlign);

        mMemIn = static_cast<int32_t>(mTable->base) + static_cast<int32_t>(mLoop.start << 1);
        mSample = static_cast<int32_t>(mLoop.start);
        int32_t op = outp;

        while (outCount > nSam) {
            op += nSam << 1;
            outCount -= nSam;
            if (mLoop.count != 0xFFFFFFFFu && mLoop.count != 0) {
                mLoop.count--;
            }

            nSam = std::min<int32_t>(outCount, static_cast<int32_t>(mLoop.end - mLoop.start));
            nbytes = nSam << 1;

            const uint8_t* dramLoc = Dma(mMemIn, nbytes);
            dramAlign = static_cast<int>(reinterpret_cast<uintptr_t>(dramLoc) & 7);
            nbytes += dramAlign;
            const int dmemAlign = (op & 7) ? 8 - (op & 7) : 0;

            Ucode::SetBuffer(static_cast<uint16_t>(op + dmemAlign), 0,
                             static_cast<uint16_t>(nbytes + 8 - (nbytes & 7)));
            Ucode::LoadBuffer(dramLoc - dramAlign);

            if (dramAlign || dmemAlign) {
                Ucode::DmemMove(static_cast<uint16_t>(op + dramAlign + dmemAlign), static_cast<uint16_t>(op),
                                static_cast<uint32_t>(nSam << 1));
            }
        }

        mSample += outCount;
        mMemIn += outCount << 1;
        return;
    }

    int32_t nbytes = outCount << 1;
    int32_t overFlow = mMemIn + nbytes - (static_cast<int32_t>(mTable->base) + mTableLength);
    if (overFlow < 0) {
        overFlow = 0;
    }
    if (overFlow > nbytes) {
        overFlow = nbytes;
    }

    if (overFlow < nbytes) {
        int dramAlign = 0;
        if (outCount > 0) {
            nbytes -= overFlow;
            const uint8_t* dramLoc = Dma(mMemIn, nbytes);
            dramAlign = static_cast<int>(reinterpret_cast<uintptr_t>(dramLoc) & 7);
            nbytes += dramAlign;
            Ucode::SetBuffer(outp, 0, static_cast<uint16_t>(nbytes + 8 - (nbytes & 7)));
            Ucode::LoadBuffer(dramLoc - dramAlign);
        }
        outp = static_cast<uint16_t>(outp + dramAlign);

        mSample += outCount;
        mMemIn += outCount << 1;
    } else {
        mMemIn += outCount << 1;
    }

    if (overFlow) {
        int32_t startZero = (outCount << 1) - overFlow;
        if (startZero < 0) {
            startZero = 0;
        }
        Ucode::ClearBuffer(static_cast<uint16_t>(startZero + outp), static_cast<uint32_t>(overFlow));
    }
}

// ---------------------------------------------------------------------------
// Resampler

void Resampler::SetParam(int paramId, const Wave* wave, float floatValue) {
    switch (paramId) {
        case kReset:
            mDelta = 0.0f;
            mFirst = 1;
            mMotion = kStopped;
            mUnityPitch = 0;
            mSource->SetParam(kReset, nullptr);
            break;
        case kStart:
            mMotion = kPlaying;
            mSource->SetParam(kStart, nullptr);
            break;
        case kSetPitch:
            mRatio = floatValue;
            break;
        case kSetUnityPitch:
            mUnityPitch = 1;
            break;
        default:
            mSource->SetParam(paramId, wave);
            break;
    }
}

void Resampler::Pull(uint16_t& outp, int outCnt, int sampleOffset) {
    (void)sampleOffset;
    uint16_t inp = kDecoderOut;
    if (!outCnt) {
        return;
    }

    if (mUnityPitch) {
        mSource->Pull(inp, outCnt);
        Ucode::DmemMove(inp, outp, static_cast<uint32_t>(outCnt << 1));
        return;
    }

    if (mRatio > kMaxRatio) {
        mRatio = kMaxRatio;
    }

    // Quantize the ratio to what the ucode sees.
    mRatio = static_cast<float>(static_cast<int32_t>(mRatio * kUnityPitch));
    mRatio = mRatio / kUnityPitch;

    const float finCount = mDelta + (mRatio * static_cast<float>(outCnt));
    const int32_t inCount = static_cast<int32_t>(finCount);
    mDelta = finCount - static_cast<float>(inCount);

    mSource->Pull(inp, inCount);

    const int32_t incr = static_cast<int32_t>(mRatio * kUnityPitch);
    Ucode::SetBuffer(inp, outp, static_cast<uint16_t>(outCnt << 1));
    Ucode::Resample(mFirst ? uint32_t(Ucode::kResampleInit) : 0u, static_cast<uint16_t>(incr), &mState);
    mFirst = 0;
}

// ---------------------------------------------------------------------------
// Envelope mixer

void EnvMixer::AddUpdate(Param* update) {
    if (mCtrlTail) {
        mCtrlTail->next = update;
    } else {
        mCtrlList = update;
    }
    mCtrlTail = update;
}

void EnvMixer::SetParam(int paramId, Param* update, const Wave* wave, float floatValue) {
    switch (paramId) {
        case kAddUpdate:
            AddUpdate(update);
            break;
        case kReset:
            mFirst = 1;
            mMotion = kStopped;
            mVolume = 1;
            mSource->SetParam(kReset, nullptr, 0.0f);
            break;
        case kStart:
            mMotion = kPlaying;
            mSource->SetParam(kStart, nullptr, 0.0f);
            break;
        default:
            mSource->SetParam(paramId, wave, floatValue);
            break;
    }
}

void EnvMixer::PullSubFrame(uint16_t& inp, uint16_t outp, int outCount, int sampleOffset) {
    if (!outCount) {
        return;
    }

    mSource->Pull(inp, outCount, sampleOffset);

    Ucode::SetBuffer(inp, static_cast<uint16_t>(kMainLOut + outp), static_cast<uint16_t>(outCount << 1));
    Ucode::SetAuxBuffer(static_cast<uint16_t>(kMainROut + outp), static_cast<uint16_t>(kAuxLOut + outp),
                        static_cast<uint16_t>(kAuxROut + outp));

    if (mFirst) {
        mFirst = 0;

        // Derived ramp parameters.
        mLtgt = static_cast<int16_t>((mVolume * kEqPower[mPan]) >> 15);
        mLratm = GetRate(mCvolL, mLtgt, mSegEnd, &mLratl);
        mRtgt = static_cast<int16_t>((mVolume * kEqPower[kEqPowerLength - mPan - 1]) >> 15);
        mRratm = GetRate(mCvolR, mRtgt, mSegEnd, &mRratl);

        Ucode::SetVolume(Ucode::kVolumeLeft | Ucode::kVolumeVol, static_cast<uint16_t>(mCvolL), 0, 0);
        Ucode::SetVolume(Ucode::kVolumeVol, static_cast<uint16_t>(mCvolR), 0, 0);
        Ucode::SetVolume(Ucode::kVolumeLeft, static_cast<uint16_t>(mLtgt), static_cast<uint16_t>(mLratm), mLratl);
        Ucode::SetVolume(0, static_cast<uint16_t>(mRtgt), static_cast<uint16_t>(mRratm), mRratl);
        Ucode::SetVolume(Ucode::kVolumeAux, static_cast<uint16_t>(mDryAmt), 0, static_cast<uint16_t>(mWetAmt));
        Ucode::EnvMixer(Ucode::kEnvMixerInit, &mState);
    } else {
        Ucode::EnvMixer(0, &mState);
    }

    inp = static_cast<uint16_t>(inp + (outCount << 1));
}

void EnvMixer::Pull(uint16_t& outp, int outCount, int sampleOffset) {
    (void)outp;
    uint16_t inp = kResamplerOut;
    int32_t thisOffset = sampleOffset;
    uint16_t loutp = 0;

    while (mCtrlList != nullptr) {
        const int32_t lastOffset = thisOffset;
        thisOffset = mCtrlList->delta;
        const int32_t samples = thisOffset - lastOffset;
        if (samples > outCount) {
            break;
        }

        Param* update = mCtrlList;
        switch (update->type) {
            case kStartVoiceAlt:
                if (update->unity) {
                    SetParam(kSetUnityPitch, nullptr, nullptr, 0.0f);
                }

                SetParam(kSetWavetable, nullptr, update->wave, 0.0f);
                SetParam(kStart, nullptr, nullptr, 0.0f);

                mFirst = 1;
                mDelta = 0;
                mSegEnd = update->samples;

                mVolume = static_cast<int16_t>((static_cast<int32_t>(update->volume) * update->volume) >> 15);
                mPan = update->pan;
                mDryAmt = kEqPower[update->fxMix];
                mWetAmt = kEqPower[kEqPowerLength - update->fxMix - 1];

                if (update->samples) {
                    mCvolL = 1;
                    mCvolR = 1;
                } else {
                    // No attack: start at the target volume.
                    mCvolL = static_cast<int16_t>((mVolume * kEqPower[mPan]) >> 15);
                    mCvolR = static_cast<int16_t>((mVolume * kEqPower[kEqPowerLength - mPan - 1]) >> 15);
                }

                mSource->SetParam(kSetPitch, nullptr, update->pitch);
                break;

            case kSetFxAmount:
            case kSetPan:
            case kSetVolume:
                PullSubFrame(inp, loutp, samples, sampleOffset);

                mDelta += samples;

                if (mDelta >= mSegEnd) {
                    // Reached the target; recompute it in case the segment was empty.
                    mLtgt = static_cast<int16_t>((mVolume * kEqPower[mPan]) >> 15);
                    mRtgt = static_cast<int16_t>((mVolume * kEqPower[kEqPowerLength - mPan - 1]) >> 15);
                    mDelta = mSegEnd;
                    mCvolL = mLtgt;
                    mCvolR = mRtgt;
                } else {
                    // Estimate where the ramp has got to.
                    mCvolL = static_cast<int16_t>(GetVol(mCvolL, mDelta, mLratm, mLratl));
                    mCvolR = static_cast<int16_t>(GetVol(mCvolR, mDelta, mRratm, mRratl));
                }

                // A zero volume could never ramp up again.
                if (mCvolL == 0) {
                    mCvolL = 1;
                }
                if (mCvolR == 0) {
                    mCvolR = 1;
                }

                if (update->type == kSetPan) {
                    mPan = static_cast<int16_t>(update->dataInt);
                }

                if (update->type == kSetVolume) {
                    // Start a new segment; volume maps non-linearly towards loudness.
                    mDelta = 0;
                    int32_t fVol = update->dataInt;
                    fVol = (fVol * fVol) >> 15;
                    mVolume = static_cast<int16_t>(fVol);
                    mSegEnd = update->moreInt;
                }

                if (update->type == kSetFxAmount) {
                    mDryAmt = kEqPower[update->dataInt];
                    mWetAmt = kEqPower[kEqPowerLength - update->dataInt - 1];
                }

                mFirst = 1;
                break;

            case kStartVoice:
                if (update->unity) {
                    SetParam(kSetUnityPitch, nullptr, nullptr, 0.0f);
                }
                SetParam(kSetWavetable, nullptr, update->wave, 0.0f);
                SetParam(kStart, nullptr, nullptr, 0.0f);
                break;

            case kStopVoice:
                PullSubFrame(inp, loutp, samples, sampleOffset);
                SetParam(kReset, nullptr, nullptr, 0.0f);
                break;

            case kFreeVoice:
                update->pvoice->offset = 0;
                mSynth->FreePVoice(update->pvoice);
                break;

            default:
                PullSubFrame(inp, loutp, samples, sampleOffset);
                mDelta += samples;
                SetParam(update->type, nullptr, nullptr, update->dataFloat);
                break;
        }

        loutp = static_cast<uint16_t>(loutp + (samples << 1));
        outCount -= samples;

        mCtrlList = mCtrlList->next;
        if (mCtrlList == nullptr) {
            mCtrlTail = nullptr;
        }
        mSynth->FreeParam(update);
    }

    if (mMotion == kPlaying) {
        PullSubFrame(inp, loutp, outCount, sampleOffset);
        mDelta += outCount;
    }

    if (mDelta > mSegEnd) {
        mDelta = mSegEnd;
    }
}

void PVoice::SetParam(int paramId, Param* update, const Wave* wave, float floatValue) {
    envmixer.SetParam(paramId, update, wave, floatValue);
}

// ---------------------------------------------------------------------------
// Reverb

Fx::Fx(const SynConfig& config) {
    std::vector<int32_t> params;
    constexpr int32_t kMs = 40; // 1 ms in the effect tables' units (44.1 truncated to a multiple of 8)
    auto section = [&](int32_t in, int32_t out, int32_t fb, int32_t ff, int32_t gain, int32_t rate, int32_t depth,
                       int32_t lp) {
        for (int32_t v : { in, out, fb, ff, gain, rate, depth, lp }) {
            params.push_back(v);
        }
    };
    switch (config.fxType) {
        case kFxSmallRoom:
            params = { 3, 100 * kMs };
            section(0, 54 * kMs, 9830, -9830, 0, 0, 0, 0);
            section(19 * kMs, 38 * kMs, 3276, -3276, 0x3fff, 0, 0, 0);
            section(0, 60 * kMs, 5000, 0, 0, 0, 0, 0x5000);
            break;
        case kFxBigRoom:
            params = { 4, 100 * kMs };
            section(0, 66 * kMs, 9830, -9830, 0, 0, 0, 0);
            section(22 * kMs, 54 * kMs, 3276, -3276, 0x3fff, 0, 0, 0);
            section(66 * kMs, 91 * kMs, 3276, -3276, 0x3fff, 0, 0, 0);
            section(0, 94 * kMs, 8000, 0, 0, 0, 0, 0x5000);
            break;
        case kFxEcho:
            params = { 1, 200 * kMs };
            section(0, 179 * kMs, 12000, 0, 0x7fff, 0, 0, 0);
            break;
        case kFxChorus:
            params = { 1, 20 * kMs };
            section(0, 5 * kMs, 0x4000, 0, 0x7fff, 7600, 700, 0);
            break;
        case kFxFlange:
            params = { 1, 20 * kMs };
            section(0, 5 * kMs, 0, 0x5fff, 0x7fff, 380, 500, 0);
            break;
        case kFxCustom:
            params = config.fxParams;
            break;
        default:
            params = { 0, 0 };
            break;
    }

    size_t j = 0;
    const int32_t sectionCount = params[j++];
    mLength = static_cast<uint32_t>(params[j++]);
    mDelays.resize(static_cast<size_t>(sectionCount));
    // 8-byte aligned so the chorus resampler's alignment handling matches.
    mBaseStorage.assign(mLength + 8, 0);
    const uintptr_t aligned = (reinterpret_cast<uintptr_t>(mBaseStorage.data()) + 7) & ~uintptr_t(7);
    mBase = reinterpret_cast<int16_t*>(aligned);
    mInputPos = 0;

    for (FxDelay& d : mDelays) {
        d.input = static_cast<uint32_t>(params[j++]);
        d.output = static_cast<uint32_t>(params[j++]);
        d.fbcoef = static_cast<int16_t>(params[j++]);
        d.ffcoef = static_cast<int16_t>(params[j++]);
        d.gain = static_cast<int16_t>(params[j++]);

        if (params[j]) {
            d.rsinc = static_cast<float>(
                (((static_cast<float>(params[j++]) / 1000) * kChorusRange) / static_cast<float>(config.outputRate)));
            const int32_t length = static_cast<int32_t>(d.output - d.input);
            d.rsgain = static_cast<float>((static_cast<float>(params[j++]) / kChorusConvert) * length);
            d.rsval = 1.0f;
            d.rsdelta = 0;
            d.hasResampler = true;
            d.rsDelta = 0.0f;
            d.rsFirst = 1;
        } else {
            d.hasResampler = false;
            j += 2;
        }

        if (params[j]) {
            d.hasLowPass = true;
            d.lpCoefficient = static_cast<int16_t>(params[j++]);
            const int32_t temp = d.lpCoefficient * kLowPassScale;
            const int16_t fc = static_cast<int16_t>(temp >> 15);
            d.lpGain = static_cast<int16_t>(kLowPassScale - fc);
            d.lpFirst = true;
            for (int i = 0; i < 8; i++) {
                d.lpVector[i] = 0;
            }
            d.lpVector[8] = fc;
            const double ffc = static_cast<double>(fc) / kLowPassScale;
            double fcoef = ffc;
            for (int i = 9; i < 16; i++) {
                fcoef *= ffc;
                d.lpVector[i] = static_cast<int16_t>(fcoef * kLowPassScale);
            }
        } else {
            d.hasLowPass = false;
            j++;
        }
    }
}

float Fx::DoModFunc(FxDelay& d, int count) {
    // Bipolar sawtooth from -range to +range, folded to a triangle.
    d.rsval += d.rsinc * count;
    d.rsval = (d.rsval > kChorusRange) ? d.rsval - static_cast<float>(kChorusRange * 2) : d.rsval;

    float val = d.rsval;
    val = (val < 0) ? -val : val;
    val -= static_cast<float>(kChorusRange / 2);
    return d.rsgain * val;
}

void Fx::LoadBuffer(int64_t pos, uint16_t buff, int count) {
    const int64_t length = mLength;
    int64_t curr = pos;
    if (curr < 0) {
        curr += length;
    }
    curr = std::clamp<int64_t>(curr, 0, length);
    const int64_t updated = curr + count;
    if (updated > length) {
        const int64_t afterEnd = updated - length;
        const int64_t beforeEnd = length - curr;
        Ucode::SetBuffer(buff, 0, static_cast<uint16_t>(beforeEnd << 1));
        Ucode::LoadBuffer(mBase + curr);
        Ucode::SetBuffer(static_cast<uint16_t>(buff + (beforeEnd << 1)), 0, static_cast<uint16_t>(afterEnd << 1));
        Ucode::LoadBuffer(mBase);
    } else {
        Ucode::SetBuffer(buff, 0, static_cast<uint16_t>(count << 1));
        Ucode::LoadBuffer(mBase + curr);
    }
    Ucode::SetBuffer(0, 0, static_cast<uint16_t>(count << 1));
}

void Fx::SaveBuffer(int64_t pos, uint16_t buff, int count) {
    const int64_t length = mLength;
    int64_t curr = pos;
    if (curr < 0) {
        curr += length;
    }
    curr = std::clamp<int64_t>(curr, 0, length);
    const int64_t updated = curr + count;
    if (updated > length) {
        const int64_t afterEnd = updated - length;
        const int64_t beforeEnd = length - curr;
        Ucode::SetBuffer(0, buff, static_cast<uint16_t>(beforeEnd << 1));
        Ucode::SaveBuffer(mBase + curr);
        Ucode::SetBuffer(0, static_cast<uint16_t>(buff + (beforeEnd << 1)), static_cast<uint16_t>(afterEnd << 1));
        Ucode::SaveBuffer(mBase);
        Ucode::SetBuffer(0, 0, static_cast<uint16_t>(count << 1));
    } else {
        Ucode::SetBuffer(0, buff, static_cast<uint16_t>(count << 1));
        Ucode::SaveBuffer(mBase + curr);
    }
}

void Fx::FilterBuffer(FxDelay& d, uint16_t buff, int count) {
    Ucode::SetBuffer(buff, buff, static_cast<uint16_t>(count << 1));
    Ucode::LoadAdpcmBook(d.lpVector, 32);
    Ucode::PoleFilter(d.lpFirst ? 1 : 0, static_cast<uint16_t>(d.lpGain), d.lpState);
    d.lpFirst = false;
}

void Fx::LoadOutputBuffer(FxDelay& d, uint16_t buff, int incount) {
    const uint16_t rbuff = kTemp2;

    if (d.hasResampler) {
        const int32_t length = static_cast<int32_t>(d.output - d.input);
        float delta = DoModFunc(d, incount);
        // Delta as a pitch ratio, quantized to the resampler's resolution.
        delta /= length;
        delta = static_cast<float>(static_cast<int32_t>(delta * kUnityPitch));
        delta = delta / kUnityPitch;
        const float fratio = static_cast<float>(1.0 - delta);

        const float fincount = d.rsDelta + (fratio * static_cast<float>(incount));
        const int32_t count = static_cast<int32_t>(fincount);
        d.rsDelta = fincount - static_cast<float>(count);

        const int64_t outPos = mInputPos - (static_cast<int64_t>(d.output) - d.rsdelta);
        // The delay line is 8-byte aligned, so a position's alignment in
        // samples is its low two bits.
        const int ramalign = static_cast<int>(outPos & 3);

        LoadBuffer(outPos - ramalign, rbuff, count + ramalign);
        const int32_t ratio = static_cast<int32_t>(fratio * kUnityPitch);
        Ucode::SetBuffer(static_cast<uint16_t>(rbuff + (ramalign << 1)), buff, static_cast<uint16_t>(incount << 1));
        Ucode::Resample(d.rsFirst ? uint32_t(Ucode::kResampleInit) : 0u, static_cast<uint16_t>(ratio), &d.rsState);
        d.rsFirst = 0;
        d.rsdelta += count - incount;
    } else {
        LoadBuffer(mInputPos - static_cast<int64_t>(d.output), buff, incount);
    }
}

void Fx::Pull(int outCount) {
    const uint16_t input = kAuxLOut;
    const uint16_t output = kAuxROut;
    uint16_t buff1 = kTemp0;
    uint16_t buff2 = kTemp1;
    bool havePrevOut = false;
    int64_t prevOutPos = 0;

    Ucode::SetBuffer(0, 0, static_cast<uint16_t>(outCount << 1));
    Ucode::Mix(0xda83, kAuxLOut, input); // 0.707 L: L - 0.293 L
    Ucode::Mix(0x5a82, kAuxROut, input); // fold the right aux channel into the left

    // The mixed input goes to the delay line at the master input position.
    SaveBuffer(mInputPos, input, outCount);
    Ucode::ClearBuffer(output, static_cast<uint32_t>(outCount << 1));

    for (FxDelay& d : mDelays) {
        const int64_t inPos = mInputPos - static_cast<int64_t>(d.input);
        const int64_t outPos = mInputPos - static_cast<int64_t>(d.output);

        if (havePrevOut && inPos == prevOutPos) {
            std::swap(buff1, buff2);
        } else {
            LoadBuffer(inPos, buff1, outCount);
        }
        LoadOutputBuffer(d, buff2, outCount);

        if (d.ffcoef) {
            Ucode::Mix(static_cast<uint16_t>(d.ffcoef), buff1, buff2);
            if (!d.hasResampler && !d.hasLowPass) {
                SaveBuffer(outPos, buff2, outCount);
            }
        }

        if (d.fbcoef) {
            Ucode::Mix(static_cast<uint16_t>(d.fbcoef), buff2, buff1);
            SaveBuffer(inPos, buff1, outCount);
        }

        if (d.hasLowPass) {
            FilterBuffer(d, buff2, outCount);
        }

        if (!d.hasResampler) {
            SaveBuffer(outPos, buff2, outCount);
        }

        if (d.gain) {
            Ucode::Mix(static_cast<uint16_t>(d.gain), buff2, output);
        }

        prevOutPos = mInputPos + static_cast<int64_t>(d.output);
        havePrevOut = true;
    }

    // Advance the master input position around the delay line.
    mInputPos += outCount;
    if (mInputPos > static_cast<int64_t>(mLength)) {
        mInputPos -= mLength;
    }

    // The output is in the right aux channel; copy it to the left.
    Ucode::DmemMove(output, kAuxLOut, static_cast<uint32_t>(outCount << 1));
}

// ---------------------------------------------------------------------------
// Synth

Synth::Synth(const SynConfig& config) : mOutputRate(config.outputRate) {
    mVoiceStorage.reserve(static_cast<size_t>(config.maxPVoices));
    for (int i = 0; i < config.maxPVoices; i++) {
        mVoiceStorage.push_back(std::make_unique<PVoice>(this));
        // Linking at the head leaves the last voice first.
        mFreeList.push_front(mVoiceStorage.back().get());
    }

    if (config.fxType != kFxNone) {
        mFx = std::make_unique<Fx>(config);
    }

    mParamStorage.resize(static_cast<size_t>(config.maxUpdates));
    for (Param& param : mParamStorage) {
        param.next = mParamList;
        mParamList = &param;
    }

    Ucode::Reset();
}

Synth::~Synth() = default;

void Synth::AddPlayer(Player* player) {
    player->samplesLeft = mCurSamples;
    mPlayers.insert(mPlayers.begin(), player);
}

Param* Synth::AllocParam() {
    Param* update = mParamList;
    if (update != nullptr) {
        mParamList = update->next;
        *update = Param{};
    }
    return update;
}

void Synth::FreeParam(Param* param) {
    param->next = mParamList;
    mParamList = param;
}

void Synth::FreePVoice(PVoice* pvoice) {
    // Moves from the allocated list to the head of the lame list.
    auto it = std::find(mAllocList.begin(), mAllocList.end(), pvoice);
    if (it != mAllocList.end()) {
        mAllocList.erase(it);
    }
    mLameList.push_front(pvoice);
}

void Synth::CollectPVoices() {
    while (!mLameList.empty()) {
        PVoice* pv = mLameList.front();
        mLameList.pop_front();
        mFreeList.push_front(pv);
    }
}

// Half a sample is added to offset the average truncation error.
int32_t Synth::TimeToSamplesNoRound(MicroTime micros) const {
    const float tmp =
        static_cast<float>(static_cast<float>(micros) * static_cast<float>(mOutputRate) / 1000000.0 + 0.5);
    return static_cast<int32_t>(tmp);
}

int32_t Synth::TimeToSamples(MicroTime micros) const {
    return TimeToSamplesNoRound(micros) & ~0xf;
}

int32_t Synth::NextSampleTime(Player** client) {
    int32_t delta = 0x7fffffff;
    *client = nullptr;
    for (Player* cl : mPlayers) {
        if ((cl->samplesLeft - mCurSamples) < delta) {
            *client = cl;
            delta = cl->samplesLeft - mCurSamples;
        }
    }
    return (*client)->samplesLeft;
}

PVoice* Synth::AllocatePVoice(int16_t priority, bool* stolen) {
    *stolen = false;
    PVoice* pvoice = nullptr;
    if (!mLameList.empty()) {
        pvoice = mLameList.front();
        mLameList.pop_front();
        mAllocList.push_front(pvoice);
    } else if (!mFreeList.empty()) {
        pvoice = mFreeList.front();
        mFreeList.pop_front();
        mAllocList.push_front(pvoice);
    } else {
        // Steal: the last voice of equal or lower priority that is not
        // already being stolen.
        for (PVoice* pv : mAllocList) {
            if (pv->vvoice->priority <= priority && pv->offset == 0) {
                pvoice = pv;
                priority = pv->vvoice->priority;
                *stolen = true;
            }
        }
    }
    return pvoice;
}

bool Synth::AllocVoice(Voice* voice, const VoiceConfig& vc) {
    voice->priority = vc.priority;
    voice->unityPitch = vc.unityPitch;
    voice->table = nullptr;
    voice->fxBus = vc.fxBus;
    voice->state = kStopped;
    voice->pvoice = nullptr;

    bool stolen = false;
    PVoice* pvoice = AllocatePVoice(vc.priority, &stolen);
    if (pvoice == nullptr) {
        return false;
    }

    if (stolen) {
        pvoice->offset = 512;
        pvoice->vvoice->pvoice = nullptr;

        // Ramp the stolen voice down, then stop it.
        if (Param* update = AllocParam()) {
            update->delta = mParamSamples;
            update->type = kSetVolume;
            update->dataInt = 0;
            update->moreInt = pvoice->offset - 64;
            pvoice->SetParam(kAddUpdate, update);
        }
        if (Param* update = AllocParam()) {
            update->delta = mParamSamples + pvoice->offset;
            update->type = kStopVoice;
            update->next = nullptr;
            pvoice->SetParam(kAddUpdate, update);
        }
    } else {
        pvoice->offset = 0;
    }
    pvoice->vvoice = voice;
    voice->pvoice = pvoice;
    return true;
}

void Synth::FreeVoice(Voice* voice) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (voice->pvoice->offset) {
        // A stolen voice is freed once its ramp-down has run.
        if (Param* update = AllocParam()) {
            update->delta = mParamSamples + voice->pvoice->offset;
            update->type = kFreeVoice;
            update->pvoice = voice->pvoice;
            voice->pvoice->SetParam(kAddUpdate, update);
        }
    } else {
        FreePVoice(voice->pvoice);
    }
    voice->pvoice = nullptr;
}

void Synth::StartVoice(Voice* voice, const Wave* table) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->type = kStartVoice;
        update->wave = table;
        update->next = nullptr;
        update->unity = voice->unityPitch;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::StartVoiceParams(Voice* voice, const Wave* wave, float pitch, int16_t volume, uint8_t pan, uint8_t fxMix,
                             MicroTime time) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->next = nullptr;
        update->type = kStartVoiceAlt;
        update->unity = voice->unityPitch;
        update->pan = pan;
        update->volume = volume;
        update->fxMix = fxMix;
        update->pitch = pitch;
        update->samples = TimeToSamples(time);
        update->wave = wave;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::StopVoice(Voice* voice) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->type = kStopVoice;
        update->next = nullptr;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::SetVolume(Voice* voice, int16_t volume, MicroTime time) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->type = kSetVolume;
        update->dataInt = volume;
        update->moreInt = TimeToSamples(time);
        update->next = nullptr;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::SetPitch(Voice* voice, float pitch) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->type = kSetPitch;
        update->dataFloat = pitch;
        update->next = nullptr;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::SetPan(Voice* voice, uint8_t pan) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->type = kSetPan;
        update->dataInt = pan;
        update->next = nullptr;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::SetFxMix(Voice* voice, uint8_t fxMix) {
    if (voice->pvoice == nullptr) {
        return;
    }
    if (Param* update = AllocParam()) {
        update->delta = mParamSamples + voice->pvoice->offset;
        update->type = kSetFxAmount;
        update->dataInt = fxMix;
        update->next = nullptr;
        voice->pvoice->SetParam(kAddUpdate, update);
    }
}

void Synth::PullChain(int16_t* out, int nOut) {
    // Main bus: clear, then mix in each source -- the effects bus when there
    // is one, which pulls the voices through its aux bus.
    Ucode::ClearBuffer(kMainLOut, static_cast<uint32_t>(nOut << 1));
    Ucode::ClearBuffer(kMainROut, static_cast<uint32_t>(nOut << 1));

    // Aux bus: clear, then every voice's envelope mixer adds to both buses.
    Ucode::ClearBuffer(kAuxLOut, static_cast<uint32_t>(nOut << 1));
    Ucode::ClearBuffer(kAuxROut, static_cast<uint32_t>(nOut << 1));
    uint16_t unused = 0;
    for (auto& pv : mVoiceStorage) {
        pv->envmixer.Pull(unused, nOut, mCurSamples);
    }
    if (mFx) {
        mFx->Pull(nOut);
    }

    Ucode::SetBuffer(0, 0, static_cast<uint16_t>(nOut << 1));
    Ucode::Mix(0x7fff, kAuxLOut, kMainLOut);
    Ucode::Mix(0x7fff, kAuxROut, kMainROut);

    // Save: interleave the main bus and write it out.
    Ucode::SetBuffer(0, 0, static_cast<uint16_t>(nOut << 1));
    Ucode::Interleave(kMainLOut, kMainROut);
    Ucode::SetBuffer(0, 0, static_cast<uint16_t>(nOut << 2));
    Ucode::SaveBuffer(out);
}

void Synth::AudioFrame(int16_t* out, int outLen) {
    if (mPlayers.empty()) {
        std::memset(out, 0, static_cast<size_t>(outLen) * 2 * sizeof(int16_t));
        return;
    }

    // Run every player whose callback falls inside this frame. Changes they
    // make are stamped at the frame's start (rounded down to 16 samples).
    Player* client = nullptr;
    for (mParamSamples = NextSampleTime(&client); mParamSamples - mCurSamples < outLen;
         mParamSamples = NextSampleTime(&client)) {
        mParamSamples &= ~0xf;
        client->samplesLeft += TimeToSamplesNoRound(client->handler());
    }
    mParamSamples &= ~0xf;

    int16_t* lOutBuf = out;
    while (outLen > 0) {
        const int nOut = std::min(kMaxRspSamples, outLen);
        PullChain(lOutBuf, nOut);
        outLen -= nOut;
        lOutBuf += nOut << 1;
        mCurSamples += nOut;
    }

    CollectPVoices();
}

} // namespace Frigate::AudioDriver
