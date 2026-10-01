#include "AudioUcode.h"

#include <cstring>

// See AudioUcode.h and THIRD_PARTY_LICENSES.md: the DSP algorithms in this
// file (ADPCM decode, linear resample against the N64 64-phase table,
// linear envelope mixing, DMEM mix/interleave) are ported from
// `goldeneye-pc-port`'s `port/src/mixer.c` (MIT). The DMEM-addressing
// context (aSetBuffer's persistent in/out/count/dryR/wetL/wetR fields) is
// ported as-is too -- it reflects GE's own ucode ABI, not a PC-port
// invention.

namespace Frigate::AudioUcode {

namespace {

constexpr size_t kDmemSize = 4096;
uint8_t sDmem[kDmemSize];

inline uint8_t* DmemU8(uint16_t addr) {
    return sDmem + addr;
}
inline int16_t* DmemS16(uint16_t addr) {
    return reinterpret_cast<int16_t*>(sDmem + addr);
}

inline int16_t Clamp16(int32_t v) {
    if (v < -0x8000)
        return -0x8000;
    if (v > 0x7fff)
        return 0x7fff;
    return static_cast<int16_t>(v);
}

// Persistent context set by SetBuffer/SetAuxBuffer, read by opcodes that
// don't carry their own DMEM addresses/counts -- see mixer.c's call-site
// survey in THIRD_PARTY_LICENSES.md for how this was reconstructed.
struct {
    uint16_t in = 0;
    uint16_t out = 0;
    uint32_t count = 0;
    uint16_t dryR = 0;
    uint16_t wetL = 0;
    uint16_t wetR = 0;
} sCtx;

int16_t sAdpcmBook[8][2][8];
AdpcmState* sAdpcmLoopState = nullptr;

// N64 RSP's 64-phase linear resample table. Stored unsigned (values above
// 0x7fff are the two's-complement bit patterns of negative int16_t
// coefficients) since a braced int16_t initializer can't narrow them
// directly; Resample() reinterprets each entry when it reads the table.
constexpr uint16_t kResampleTable[64][4] = {
    { 0x0c39, 0x66ad, 0x0d46, 0xffdf }, { 0x0b39, 0x6696, 0x0e5f, 0xffd8 }, { 0x0a44, 0x6669, 0x0f83, 0xffd0 },
    { 0x095a, 0x6626, 0x10b4, 0xffc8 }, { 0x087d, 0x65cd, 0x11f0, 0xffbf }, { 0x07ab, 0x655e, 0x1338, 0xffb6 },
    { 0x06e4, 0x64d9, 0x148c, 0xffac }, { 0x0628, 0x643f, 0x15eb, 0xffa1 }, { 0x0577, 0x638f, 0x1756, 0xff96 },
    { 0x04d1, 0x62cb, 0x18cb, 0xff8a }, { 0x0435, 0x61f3, 0x1a4c, 0xff7e }, { 0x03a4, 0x6106, 0x1bd7, 0xff71 },
    { 0x031c, 0x6007, 0x1d6c, 0xff64 }, { 0x029f, 0x5ef5, 0x1f0b, 0xff56 }, { 0x022a, 0x5dd0, 0x20b3, 0xff48 },
    { 0x01be, 0x5c9a, 0x2264, 0xff3a }, { 0x015b, 0x5b53, 0x241e, 0xff2c }, { 0x0101, 0x59fc, 0x25e0, 0xff1e },
    { 0x00ae, 0x5896, 0x27a9, 0xff10 }, { 0x0063, 0x5720, 0x297a, 0xff02 }, { 0x001f, 0x559d, 0x2b50, 0xfef4 },
    { 0xffe2, 0x540d, 0x2d2c, 0xfee8 }, { 0xffac, 0x5270, 0x2f0d, 0xfedb }, { 0xff7c, 0x50c7, 0x30f3, 0xfed0 },
    { 0xff53, 0x4f14, 0x32dc, 0xfec6 }, { 0xff2e, 0x4d57, 0x34c8, 0xfebd }, { 0xff0f, 0x4b91, 0x36b6, 0xfeb6 },
    { 0xfef5, 0x49c2, 0x38a5, 0xfeb0 }, { 0xfedf, 0x47ed, 0x3a95, 0xfeac }, { 0xfece, 0x4611, 0x3c85, 0xfeab },
    { 0xfec0, 0x4430, 0x3e74, 0xfeac }, { 0xfeb6, 0x424a, 0x4060, 0xfeaf }, { 0xfeaf, 0x4060, 0x424a, 0xfeb6 },
    { 0xfeac, 0x3e74, 0x4430, 0xfec0 }, { 0xfeab, 0x3c85, 0x4611, 0xfece }, { 0xfeac, 0x3a95, 0x47ed, 0xfedf },
    { 0xfeb0, 0x38a5, 0x49c2, 0xfef5 }, { 0xfeb6, 0x36b6, 0x4b91, 0xff0f }, { 0xfebd, 0x34c8, 0x4d57, 0xff2e },
    { 0xfec6, 0x32dc, 0x4f14, 0xff53 }, { 0xfed0, 0x30f3, 0x50c7, 0xff7c }, { 0xfedb, 0x2f0d, 0x5270, 0xffac },
    { 0xfee8, 0x2d2c, 0x540d, 0xffe2 }, { 0xfef4, 0x2b50, 0x559d, 0x001f }, { 0xff02, 0x297a, 0x5720, 0x0063 },
    { 0xff10, 0x27a9, 0x5896, 0x00ae }, { 0xff1e, 0x25e0, 0x59fc, 0x0101 }, { 0xff2c, 0x241e, 0x5b53, 0x015b },
    { 0xff3a, 0x2264, 0x5c9a, 0x01be }, { 0xff48, 0x20b3, 0x5dd0, 0x022a }, { 0xff56, 0x1f0b, 0x5ef5, 0x029f },
    { 0xff64, 0x1d6c, 0x6007, 0x031c }, { 0xff71, 0x1bd7, 0x6106, 0x03a4 }, { 0xff7e, 0x1a4c, 0x61f3, 0x0435 },
    { 0xff8a, 0x18cb, 0x62cb, 0x04d1 }, { 0xff96, 0x1756, 0x638f, 0x0577 }, { 0xffa1, 0x15eb, 0x643f, 0x0628 },
    { 0xffac, 0x148c, 0x64d9, 0x06e4 }, { 0xffb6, 0x1338, 0x655e, 0x07ab }, { 0xffbf, 0x11f0, 0x65cd, 0x087d },
    { 0xffc8, 0x10b4, 0x6626, 0x095a }, { 0xffd0, 0x0f83, 0x6669, 0x0a44 }, { 0xffd8, 0x0e5f, 0x6696, 0x0b39 },
    { 0xffdf, 0x0d46, 0x66ad, 0x0c39 },
};

// Volume/envelope state set by SetVolume, consumed by EnvMixer's
// kEnvMixerInit branch.
struct {
    int16_t volCur[2] = { 0, 0 };
    int16_t volTgt[2] = { 0, 0 };
    int32_t volRate[2] = { 0, 0 };
    int16_t dryAmt = 0;
    int16_t wetAmt = 0;
} sVol;

} // namespace

void Reset() {
    std::memset(sDmem, 0, sizeof(sDmem));
    std::memset(sAdpcmBook, 0, sizeof(sAdpcmBook));
    sCtx = {};
    sVol = {};
    sAdpcmLoopState = nullptr;
}

void SetBuffer(uint16_t in, uint16_t out, uint16_t count) {
    sCtx.in = in;
    sCtx.out = out;
    sCtx.count = count;
}

void SetAuxBuffer(uint16_t dryR, uint16_t wetL, uint16_t wetR) {
    sCtx.dryR = dryR;
    sCtx.wetL = wetL;
    sCtx.wetR = wetR;
}

void ClearBuffer(uint16_t dmemAddr, uint32_t count) {
    std::memset(DmemU8(dmemAddr), 0, count);
}

void LoadBuffer(const void* src) {
    std::memcpy(DmemU8(sCtx.in), src, sCtx.count);
}

void SaveBuffer(void* dst) {
    std::memcpy(dst, DmemU8(sCtx.out), sCtx.count);
}

void DmemMove(uint16_t in, uint16_t out, uint32_t count) {
    std::memmove(DmemU8(out), DmemU8(in), count);
}

void LoadAdpcmBook(const void* src, uint32_t count) {
    // `count` is GE's bookSize in bytes (2 * order * npredictors * 8);
    // order=2 always for this format, matching sAdpcmBook's shape.
    uint32_t n = count;
    if (n > sizeof(sAdpcmBook))
        n = sizeof(sAdpcmBook);
    std::memcpy(sAdpcmBook, src, n);
}

void SetLoopState(AdpcmState* state) {
    sAdpcmLoopState = state;
}

void AdpcmDecode(uint32_t flags, AdpcmState* state) {
    const uint8_t* in = DmemU8(sCtx.in);
    int16_t* out = DmemS16(sCtx.out);
    int32_t nbytes = static_cast<int32_t>((sCtx.count + 31) & ~31u); // round up to 16-sample (32-byte) chunks

    if (flags & kAdpcmInit) {
        std::memset(out, 0, 16 * sizeof(int16_t));
    } else if (flags & kAdpcmLoop) {
        std::memcpy(out, sAdpcmLoopState->words, 16 * sizeof(int16_t));
    } else {
        std::memcpy(out, state->words, 16 * sizeof(int16_t));
    }
    out += 16;

    while (nbytes > 0) {
        int shift = *in >> 4;
        int tableIndex = *in++ & 0xf;
        const int16_t(*tbl)[8] = sAdpcmBook[tableIndex];

        for (int i = 0; i < 2; i++) {
            int16_t ins[8];
            int16_t prev1 = out[-1];
            int16_t prev2 = out[-2];

            for (int j = 0; j < 4; j++) {
                ins[j * 2] = static_cast<int16_t>((((*in >> 4) << 28) >> 28) << shift);
                ins[j * 2 + 1] = static_cast<int16_t>((((*in++ & 0xf) << 28) >> 28) << shift);
            }
            for (int j = 0; j < 8; j++) {
                int32_t acc = tbl[0][j] * prev2 + tbl[1][j] * prev1 + (static_cast<int32_t>(ins[j]) << 11);
                for (int k = 0; k < j; k++) {
                    acc += tbl[1][(j - k) - 1] * ins[k];
                }
                acc >>= 11;
                *out++ = Clamp16(acc);
            }
        }
        nbytes -= 16 * static_cast<int32_t>(sizeof(int16_t));
    }

    std::memcpy(state->words, out - 16, 16 * sizeof(int16_t));
}

void Resample(uint32_t flags, uint16_t pitch, ResampleState* state) {
    int16_t* stateWords = state->words;
    int16_t tmp[16];
    int16_t* inInitial = DmemS16(sCtx.in);
    int16_t* in = inInitial;
    int16_t* out = DmemS16(sCtx.out);
    int32_t nbytes = static_cast<int32_t>((sCtx.count + 15) & ~15u);
    uint32_t pitchAccumulator;

    if (flags & kResampleInit) {
        std::memset(tmp, 0, 5 * sizeof(int16_t));
    } else {
        std::memcpy(tmp, stateWords, 16 * sizeof(int16_t));
    }
    in -= 4;
    pitchAccumulator = static_cast<uint16_t>(tmp[4]);
    std::memcpy(in, tmp, 4 * sizeof(int16_t));

    do {
        for (int i = 0; i < 8; i++) {
            const uint16_t* tblRaw = kResampleTable[(pitchAccumulator * 64) >> 16];
            int16_t tbl[4] = { static_cast<int16_t>(tblRaw[0]), static_cast<int16_t>(tblRaw[1]),
                               static_cast<int16_t>(tblRaw[2]), static_cast<int16_t>(tblRaw[3]) };
            int32_t sample = ((in[0] * tbl[0] + 0x4000) >> 15) + ((in[1] * tbl[1] + 0x4000) >> 15) +
                             ((in[2] * tbl[2] + 0x4000) >> 15) + ((in[3] * tbl[3] + 0x4000) >> 15);
            *out++ = Clamp16(sample);

            pitchAccumulator += static_cast<uint32_t>(pitch) << 1;
            in += pitchAccumulator >> 16;
            pitchAccumulator %= 0x10000;
        }
        nbytes -= 8 * static_cast<int32_t>(sizeof(int16_t));
    } while (nbytes > 0);

    stateWords[4] = static_cast<int16_t>(pitchAccumulator);
    std::memcpy(stateWords, in, 4 * sizeof(int16_t));
    int32_t i = static_cast<int32_t>((in - inInitial + 4) & 7);
    in -= i;
    if (i != 0)
        i = -8 - i;
    stateWords[5] = static_cast<int16_t>(i);
    std::memcpy(stateWords + 8, in, 8 * sizeof(int16_t));
}

void Interleave(uint16_t left, uint16_t right) {
    const int16_t* lp = DmemS16(left);
    const int16_t* rp = DmemS16(right);
    int16_t* d = DmemS16(sCtx.out);
    uint32_t n = sCtx.count >> 1; // mono sample count

    for (uint32_t i = 0; i < n; i++) {
        *d++ = *lp++;
        *d++ = *rp++;
    }
}

void Mix(uint16_t gain, uint16_t in, uint16_t out) {
    const int16_t* inp = DmemS16(in);
    int16_t* outp = DmemS16(out);
    uint32_t n = sCtx.count >> 1;

    for (uint32_t i = 0; i < n; i++) {
        int32_t sample = (static_cast<int32_t>(*outp) * 0x7fff +
                          static_cast<int32_t>(*inp++) * static_cast<int16_t>(gain) + 0x4000) >>
                         15;
        *outp++ = Clamp16(sample);
    }
}

void SetVolume(uint32_t flags, uint16_t value, uint16_t rateHi, uint16_t rateLo) {
    if (flags & kVolumeAux) {
        sVol.dryAmt = static_cast<int16_t>(value);
        sVol.wetAmt = static_cast<int16_t>(rateLo);
    } else if (flags & kVolumeVol) {
        int ch = (flags & kVolumeLeft) ? 0 : 1;
        sVol.volCur[ch] = static_cast<int16_t>(value);
    } else {
        int ch = (flags & kVolumeLeft) ? 0 : 1;
        sVol.volTgt[ch] = static_cast<int16_t>(value);
        sVol.volRate[ch] = (static_cast<uint32_t>(rateHi) << 16) | rateLo;
    }
}

void EnvMixer(uint32_t flags, EnvMixerState* saved) {
    const int16_t* in = DmemS16(sCtx.in);
    int16_t* dry[2] = { DmemS16(sCtx.out), DmemS16(sCtx.dryR) };
    int16_t* wet[2] = { DmemS16(sCtx.wetL), DmemS16(sCtx.wetR) };
    uint32_t nsamples = sCtx.count >> 1;

    int32_t t[2], tgt[2], rate[2];
    int16_t volDry, volWet;

    if (flags & kEnvMixerInit) {
        for (int j = 0; j < 2; j++) {
            t[j] = sVol.volCur[j] << 16;
            rate[j] = sVol.volRate[j] >> 3;
            tgt[j] = sVol.volTgt[j] << 16;
        }
        volDry = sVol.dryAmt;
        volWet = sVol.wetAmt;
    } else {
        for (int j = 0; j < 2; j++) {
            t[j] = saved->t[j];
            rate[j] = saved->rate[j];
            tgt[j] = saved->tgt[j] << 16;
        }
        volDry = saved->volDry;
        volWet = saved->volWet;
    }

    for (uint32_t i = 0; i < nsamples; i++) {
        int16_t gain[4];
        int16_t vol[2];
        int16_t insamp = in[i];

        for (int j = 0; j < 2; j++) {
            // A ramp towards a near-full-scale target can step past what 32
            // bits hold before the target test sees it.
            const int64_t next = static_cast<int64_t>(t[j]) + rate[j];
            if ((rate[j] <= 0 && next <= tgt[j]) || (rate[j] > 0 && next >= tgt[j])) {
                t[j] = tgt[j];
                rate[j] = 0;
            } else {
                t[j] = static_cast<int32_t>(next);
            }
            vol[j] = static_cast<int16_t>(t[j] >> 16);
        }

        gain[0] = Clamp16((static_cast<int32_t>(vol[0]) * volDry + 0x4000) >> 15);
        gain[1] = Clamp16((static_cast<int32_t>(vol[1]) * volDry + 0x4000) >> 15);
        gain[2] = Clamp16((static_cast<int32_t>(vol[0]) * volWet + 0x4000) >> 15);
        gain[3] = Clamp16((static_cast<int32_t>(vol[1]) * volWet + 0x4000) >> 15);

        dry[0][i] = Clamp16(dry[0][i] + ((static_cast<int32_t>(insamp) * gain[0]) >> 15));
        dry[1][i] = Clamp16(dry[1][i] + ((static_cast<int32_t>(insamp) * gain[1]) >> 15));
        wet[0][i] = Clamp16(wet[0][i] + ((static_cast<int32_t>(insamp) * gain[2]) >> 15));
        wet[1][i] = Clamp16(wet[1][i] + ((static_cast<int32_t>(insamp) * gain[3]) >> 15));
    }

    for (int j = 0; j < 2; j++) {
        saved->t[j] = t[j];
        saved->rate[j] = rate[j];
        saved->tgt[j] = static_cast<int16_t>(tgt[j] >> 16);
    }
    saved->volDry = volDry;
    saved->volWet = volWet;
}

void PoleFilter(uint32_t flags, uint16_t gain, void* state) {
    // TODO(https://github.com/liz-baker/frigate/issues/243): the low-pass.
    // Identity passthrough for now: DMEM is left untouched, which is the
    // correct no-op for this filter shape.
    (void)flags;
    (void)gain;
    (void)state;
}

const int16_t* DmemSamples(uint16_t dmemAddr) {
    return DmemS16(dmemAddr);
}

} // namespace Frigate::AudioUcode
