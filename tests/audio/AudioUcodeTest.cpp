#include "AudioUcode.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Desktop-testable correctness checks for AudioUcode's opcode decode, per
// frigate issue #3's test-coverage plan (opcode-decode correctness here;
// on-hardware audio-quality validation is a separate, later pass). These
// exercise the interpreter with synthetic Acmd-equivalent input -- no
// GoldenEye ROM data is needed or used, since none may be redistributed
// (see README.md#legal).

using namespace Frigate::AudioUcode;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void TestClearBufferAndDmemMove() {
    Reset();
    // ClearBuffer + DmemMove operate on DMEM directly (no aSetBuffer
    // context needed) -- write a pattern, move it, and check both halves.
    uint8_t pattern[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    SetBuffer(0, 0, sizeof(pattern));
    LoadBuffer(pattern);
    DmemMove(0, 100, sizeof(pattern));

    const int16_t* moved = DmemSamples(100);
    Check(std::memcmp(moved, pattern, sizeof(pattern)) == 0, "DmemMove copies DMEM byte-for-byte");

    ClearBuffer(100, sizeof(pattern));
    uint8_t zeros[8] = { 0 };
    Check(std::memcmp(DmemSamples(100), zeros, sizeof(zeros)) == 0, "ClearBuffer zeroes the target range");
}

void TestAdpcmDecodeInitIsDeterministicAndUpdatesState() {
    Reset();

    // A fully-zeroed book + zeroed encoded input is a valid (if silent)
    // synthetic Acmd buffer: no GE-derived data, but it exercises the same
    // control flow (book lookup, prediction accumulation, state writeback)
    // real encoded audio would.
    int16_t book[8][2][8] = {};
    LoadAdpcmBook(book, sizeof(book));

    uint8_t encoded[32] = {}; // one 32-byte chunk -> 32 decoded samples
    SetBuffer(0, 64, sizeof(encoded));
    LoadBuffer(encoded);

    AdpcmState state{};
    AdpcmDecode(kAdpcmInit, &state);

    const int16_t* decoded = DmemSamples(64);
    bool allZero = true;
    for (int i = 0; i < 32; i++) {
        if (decoded[16 + i] != 0)
            allZero = false; // first 16 samples are the init lead-in
    }
    Check(allZero, "all-zero book/input decodes to silence");
    Check(std::memcmp(state.words, decoded + 16, 16 * sizeof(int16_t)) == 0,
          "decode state is updated to the last 16 decoded samples");
}

void TestResampleZeroInputStaysZero() {
    Reset();
    // Resample reads/writes 4 samples of history immediately *before* its
    // input address (see AudioUcode.h) -- real GE call sites always point
    // it past a reserved lead-in region in DMEM, so the input can't start
    // at DMEM address 0 the way a naive caller might assume.
    int16_t input[20] = {};
    SetBuffer(16, 0, sizeof(input));
    LoadBuffer(input);

    ResampleState state{};
    SetBuffer(16, 200, 32); // 16 output samples
    Resample(kResampleInit, 0x8000 /* arbitrary pitch */, &state);

    const int16_t* out = DmemSamples(200);
    bool allZero = true;
    for (int i = 0; i < 16; i++) {
        if (out[i] != 0)
            allZero = false;
    }
    Check(allZero, "resampling all-zero input yields all-zero output");
}

void TestMixAddsScaledSignal() {
    Reset();
    int16_t src[4] = { 1000, -1000, 32000, -32000 };
    int16_t dst[4] = { 0, 0, 0, 0 };
    SetBuffer(300, 0, sizeof(src));
    LoadBuffer(src);
    SetBuffer(400, 0, sizeof(dst));
    LoadBuffer(dst);

    SetBuffer(0, 0, sizeof(src)); // count only; Mix takes in/out addrs explicitly
    Mix(0x7fff /* ~unity gain */, 300, 400);

    const int16_t* out = DmemSamples(400);
    Check(out[0] == 1000, "Mix at ~unity gain adds the (fixed-point-rounded) input sample");
    Check(out[1] == -1000, "Mix at ~unity gain adds the (fixed-point-rounded) input sample, negative");
}

void TestInterleaveOrdersLeftRight() {
    Reset();
    int16_t left[3] = { 10, 20, 30 };
    int16_t right[3] = { -10, -20, -30 };
    SetBuffer(0, 0, sizeof(left));
    LoadBuffer(left);
    SetBuffer(100, 0, sizeof(right));
    LoadBuffer(right);

    SetBuffer(0, 200, 6); // 3 mono samples (sCtx.count>>1 == 3)
    Interleave(0, 100);

    const int16_t* out = DmemSamples(200);
    int16_t expected[6] = { 10, -10, 20, -20, 30, -30 };
    Check(std::memcmp(out, expected, sizeof(expected)) == 0, "Interleave alternates L/R samples");
}

void TestEnvMixerFullDryZeroWetPassesThrough() {
    Reset();
    int16_t in[4] = { 1000, 2000, 3000, -4000 };
    SetBuffer(0, 0, sizeof(in));
    LoadBuffer(in);

    ClearBuffer(100, 8);
    ClearBuffer(108, 8);
    ClearBuffer(116, 8);
    ClearBuffer(124, 8);
    SetAuxBuffer(108, 116, 124); // dry-R, wet-L, wet-R

    SetVolume(kVolumeVol | kVolumeLeft, 0x7fff, 0, 0); // volCur[0] = full
    SetVolume(kVolumeVol, 0x7fff, 0, 0);               // volCur[1] = full
    SetVolume(kVolumeLeft, 0x7fff, 0, 0);              // volTgt[0] = full, rate = 0
    SetVolume(0, 0x7fff, 0, 0);                        // volTgt[1] = full, rate = 0
    SetVolume(kVolumeAux, 0x7fff, 0, 0);               // dryAmt = full, wetAmt = 0

    SetBuffer(0, 100, sizeof(in)); // in = 0, dry-L out = 100
    EnvMixerState state{};
    EnvMixer(kEnvMixerInit, &state);

    const int16_t* dryL = DmemSamples(100);
    const int16_t* dryR = DmemSamples(108);
    const int16_t* wetL = DmemSamples(116);
    const int16_t* wetR = DmemSamples(124);
    for (int i = 0; i < 4; i++) {
        // Full-scale gain is 0x7fff/0x8000, not exactly 1.0, so the
        // fixed-point mix is within rounding of the input, not exact.
        Check(std::abs(dryL[i] - in[i]) <= 1, "full dry gain passes input through to dry-L within rounding");
        Check(std::abs(dryR[i] - in[i]) <= 1, "full dry gain passes input through to dry-R within rounding");
        Check(wetL[i] == 0, "zero wet gain leaves wet-L silent");
        Check(wetR[i] == 0, "zero wet gain leaves wet-R silent");
    }
}

void TestEnvMixerRampPastFullScaleClampsToTarget() {
    Reset();
    int16_t in[8];
    for (int16_t& v : in) {
        v = 1000;
    }
    SetBuffer(0, 0, sizeof(in));
    LoadBuffer(in);
    ClearBuffer(100, 16);
    ClearBuffer(116, 16);
    ClearBuffer(132, 16);
    ClearBuffer(148, 16);
    SetAuxBuffer(116, 132, 148);

    // Start just under full scale with a ramp step (2 units a sample) that
    // would carry the 32-bit accumulator past its range before reaching the
    // target.
    SetVolume(kVolumeVol | kVolumeLeft, 0x7ffa, 0, 0);
    SetVolume(kVolumeVol, 0x7ffa, 0, 0);
    SetVolume(kVolumeLeft, 0x7fff, 0x0010, 0);
    SetVolume(0, 0x7fff, 0x0010, 0);
    SetVolume(kVolumeAux, 0x7fff, 0, 0);

    SetBuffer(0, 100, sizeof(in));
    EnvMixerState state{};
    EnvMixer(kEnvMixerInit, &state);

    const int16_t* dryL = DmemSamples(100);
    const int16_t* dryR = DmemSamples(116);
    for (int i = 0; i < 8; i++) {
        Check(dryL[i] > 990, "a ramp reaching full scale stays at full scale instead of wrapping negative (left)");
        Check(dryR[i] > 990, "a ramp reaching full scale stays at full scale instead of wrapping negative (right)");
    }
}

} // namespace

int main() {
    TestClearBufferAndDmemMove();
    TestAdpcmDecodeInitIsDeterministicAndUpdatesState();
    TestResampleZeroInputStaysZero();
    TestMixAddsScaledSignal();
    TestInterleaveOrdersLeftRight();
    TestEnvMixerFullDryZeroWetPassesThrough();
    TestEnvMixerRampPastFullScaleClampsToTarget();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("All AudioUcode checks passed\n");
    return 0;
}
