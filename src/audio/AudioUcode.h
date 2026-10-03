#pragma once

#include <cstddef>
#include <cstdint>

// Software interpreter for GoldenEye 007's RSP audio microcode (`aspMain`).
//
// `aspMain` itself was never decompiled -- unlike GE's graphics microcode,
// it's still an opaque binary blob in the ROM, and no portable C reference
// for it has ever existed to decompile (see frigate issue #3). The opcode
// implementations below are adapted from `goldeneye-pc-port`'s from-scratch
// reimplementation of that ucode's DSP behavior (`port/src/mixer.c`,
// `port/include/mixer.h`) -- see THIRD_PARTY_LICENSES.md for attribution
// and exactly what was carried over versus adapted.
//
// This repo has no libultra segment/physical-address system yet (that's
// part of the not-yet-started decomp build integration), so unlike the
// original, opcodes that take a DRAM or state address here take a real
// host pointer instead -- callers resolve addressing themselves.
namespace Frigate::AudioUcode {

// GE's ADPCM_STATE / RESAMPLE_STATE are both 16 halfwords of opaque
// per-voice history the ucode carries across calls.
struct AdpcmState {
    int16_t words[16];
};

struct ResampleState {
    int16_t words[16];
};

// Per-voice envelope-mixer state carried across aEnvMixer calls (matches
// the layout mixer.c's aEnvMixerImpl casts its state pointer to).
struct EnvMixerState {
    int32_t t[2];
    int32_t rate[2];
    int16_t tgt[2];
    int16_t volDry;
    int16_t volWet;
};

enum SetVolumeFlags : uint32_t {
    kVolumeAux = 1u << 0,  // set dry/wet send amounts instead of a channel target
    kVolumeVol = 1u << 1,  // set a channel's current volume directly (vs. its target+rate)
    kVolumeLeft = 1u << 2, // channel 0 (vs. channel 1) for the two flags above
};

enum AdpcmFlags : uint32_t {
    kAdpcmInit = 1u << 0, // zero the decode history instead of restoring it
    kAdpcmLoop = 1u << 1, // restore decode history from the loop state, not `state`
};

enum ResampleFlags : uint32_t {
    kResampleInit = 1u << 0, // zero the resample history instead of restoring it
};

enum EnvMixerFlags : uint32_t {
    kEnvMixerInit = 1u << 0, // start volume ramps from SetVolume's targets, not saved state
};

// Resets all interpreter state (DMEM scratch, ADPCM book, volume state).
// Call once before decoding a fresh audio stream.
void Reset();

// aSetBuffer: sets the persistent DMEM in/out/count (and, with
// kVolumeAux-shaped calls via the `aux` overload, the extra dry-R/wet-L/
// wet-R addresses) context that the opcodes below without explicit DMEM
// addresses read.
void SetBuffer(uint16_t in, uint16_t out, uint16_t count);
void SetAuxBuffer(uint16_t dryR, uint16_t wetL, uint16_t wetR);

void ClearBuffer(uint16_t dmemAddr, uint32_t count);
void LoadBuffer(const void* src); // reads sCtx.count bytes into DMEM at sCtx.in
void SaveBuffer(void* dst);       // writes sCtx.count bytes from DMEM at sCtx.out
void DmemMove(uint16_t in, uint16_t out, uint32_t count);

void LoadAdpcmBook(const void* src, uint32_t count);
void SetLoopState(AdpcmState* state);
void AdpcmDecode(uint32_t flags, AdpcmState* state);

// Reads/writes 4 samples (8 bytes) of history immediately *before* the
// DMEM address set by SetBuffer's `in`, same as the original ucode --
// callers must leave that much valid space ahead of `in` (real GE call
// sites always point it past a reserved lead-in region; this is not
// optional headroom).
void Resample(uint32_t flags, uint16_t pitch, ResampleState* state);

void Interleave(uint16_t left, uint16_t right);
void Mix(uint16_t gain, uint16_t in, uint16_t out);

void SetVolume(uint32_t flags, uint16_t value, uint16_t rateHi, uint16_t rateLo);
void EnvMixer(uint32_t flags, EnvMixerState* state);

// Reverb low-pass: filters sCtx.in to sCtx.out, rounding the count up to
// whole 8-sample frames. The 16 coefficient words come from LoadAdpcmBook;
// `state` is 16 int16 words. `flags` kPoleFilterInit clears the history.
constexpr uint32_t kPoleFilterInit = 1;
void PoleFilter(uint32_t flags, uint16_t gain, void* state);

// Read-only access to the DMEM scratch buffer, for a caller to pull a
// finished mix out after the opcode sequence that produced it (typically
// ClearBuffer, ..., Interleave, then a read here) and hand it to
// AudioSink. Bounds are the caller's responsibility, same as real DMEM
// addressing.
const int16_t* DmemSamples(uint16_t dmemAddr);

} // namespace Frigate::AudioUcode
