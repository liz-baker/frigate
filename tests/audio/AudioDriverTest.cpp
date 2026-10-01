#include "AudioBank.h"
#include "AudioEngine.h"
#include "BankBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Checks the synth and sound-effect player end to end on synthetic banks: a
// sound is started through the player, the engine renders frames, and the
// PCM is measured. The banks hold constant-level ADPCM (see SoundSpec) so
// expected levels follow from the driver's own volume, pan and mix tables.

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

struct Peaks {
    int left = 0;
    int right = 0;
    bool Silent() const {
        return left == 0 && right == 0;
    }
};

Peaks PeakOf(const std::vector<int16_t>& frame) {
    Peaks p;
    for (size_t i = 0; i + 1 < frame.size(); i += 2) {
        p.left = std::max(p.left, std::abs(int(frame[i])));
        p.right = std::max(p.right, std::abs(int(frame[i + 1])));
    }
    return p;
}

struct Rig {
    AudioBank bank;
    std::unique_ptr<AudioEngine> engine;
    std::vector<int16_t> frame = std::vector<int16_t>(kFrameSamples * 2);

    explicit Rig(const std::vector<SoundSpec>& specs, EngineConfig config = EngineConfig()) {
        const std::vector<uint8_t> payload = BuildToneBank(specs);
        const bool ok = ParseAudioBankPayload(payload.data(), payload.size(), &bank);
        Check(ok, "the tone bank parses");
        engine = std::make_unique<AudioEngine>(bank, config);
    }

    // Renders one frame and returns its peaks.
    Peaks Step() {
        engine->RenderFrame(frame.data());
        return PeakOf(frame);
    }
};

// The level a steady sound settles at, from the volume, pan and mix tables.
int ExpectedLevel(const SoundSpec& spec, bool right) {
    static const int16_t eq[128] = {
        32767, 32764, 32757, 32744, 32727, 32704, 32677, 32644, 32607, 32564, 32517, 32464, 32407, 32344, 32277, 32205,
        32127, 32045, 31958, 31866, 31770, 31668, 31561, 31450, 31334, 31213, 31087, 30957, 30822, 30682, 30537, 30388,
        30234, 30075, 29912, 29744, 29572, 29395, 29214, 29028, 28838, 28643, 28444, 28241, 28033, 27821, 27605, 27385,
        27160, 26931, 26698, 26461, 26220, 25975, 25726, 25473, 25216, 24956, 24691, 24423, 24151, 23875, 23596, 23313,
        23026, 22736, 22442, 22145, 21845, 21541, 21234, 20924, 20610, 20294, 19974, 19651, 19325, 18997, 18665, 18331,
        17993, 17653, 17310, 16965, 16617, 16266, 15913, 15558, 15200, 14840, 14477, 14113, 13746, 13377, 13006, 12633,
        12258, 11881, 11503, 11122, 10740, 10357, 9971,  9584,  9196,  8806,  8415,  8023,  7630,  7235,  6839,  6442,
        6044,  5646,  5246,  4845,  4444,  4042,  3640,  3237,  2833,  2429,  2025,  1620,  1216,  810,   405,   0,
    };
    // Player: slot volume 0x7fff, state volume 0x7fff.
    int volume = std::max(0, 32767 * (spec.decayVolume * 32767 * spec.sampleVolume / 16129) / 32767 - 1);
    volume = (volume * volume) >> 15;
    const int pan = std::min(std::max(64 + spec.samplePan - 64, 0), 127);
    const int fxMix = std::min(127, std::max(0, (spec.keyMax & 0xF) * 8));
    const int gain = (volume * eq[right ? 127 - pan - 1 : pan]) >> 15;
    const int dry = (gain * eq[fxMix] + 0x4000) >> 15;
    return (spec.Level() * dry) >> 15;
}

bool Near(int value, int expected, double tolerance) {
    return std::abs(value - expected) <= std::max(2.0, std::abs(expected) * tolerance);
}

void TestSoundReachesItsLevelAndEnds() {
    SoundSpec spec;
    spec.frames = 2000; // long enough not to end by itself
    Rig rig({ spec });

    SoundState* state = rig.engine->PlaySfx(1);
    Check(state != nullptr, "PlaySfx returns a state");
    Check(rig.engine->Sfx().ActiveStates() == 1, "one active state");

    Peaks last;
    int steadyFrame = -1;
    for (int i = 0; i < 40 && steadyFrame < 0; i++) {
        last = rig.Step();
        if (Near(last.left, ExpectedLevel(spec, false), 0.03) && Near(last.right, ExpectedLevel(spec, true), 0.03)) {
            steadyFrame = i;
        }
    }
    Check(steadyFrame >= 0, "the sound settles at the level the volume, pan and mix tables give");
    Check(rig.engine->Sfx().AllocatedVoices() == 1, "one voice allocated while playing");
    Check(rig.engine->Sfx().GetPlayingState(state) != 0, "the state reports playing");

    // Attack 100 ms + decay 100 ms + release 100 ms, then the state is freed.
    int frames = steadyFrame + 1;
    while (rig.engine->Sfx().ActiveStates() > 0 && frames < 60) {
        rig.Step();
        frames++;
    }
    Check(rig.engine->Sfx().ActiveStates() == 0, "the sound finishes by itself");
    Check(rig.engine->Sfx().AllocatedVoices() == 0, "its voice is released");
    Check(frames >= 8 && frames <= 14, "the sound lasts about its attack, decay and release times");
    for (int i = 0; i < 3; i++) {
        rig.Step();
    }
    Check(rig.Step().Silent(), "the output is silent afterwards");
}

void TestAttackRampsUp() {
    SoundSpec spec;
    spec.attackTime = 300000;
    spec.frames = 2000;
    Rig rig({ spec });
    rig.engine->PlaySfx(1);

    int previous = 0;
    bool rising = true;
    int firstNonZero = -1;
    for (int i = 0; i < 6; i++) {
        const int peak = rig.Step().left;
        if (peak > 0 && firstNonZero < 0) {
            firstNonZero = i;
        }
        if (peak < previous) {
            rising = false;
        }
        previous = peak;
    }
    Check(rising, "the level rises through a 300 ms attack");
    Check(firstNonZero >= 0 && firstNonZero <= 1, "the sound starts in the first frames");
}

void TestPanEventMovesSoundLeft() {
    // A sound that holds (decay time -1) has only its short attack as a
    // volume segment; a pan change ramps over a segment's length, which a
    // long decay would stretch to seconds.
    SoundSpec spec;
    spec.frames = 8;
    spec.looped = true;
    spec.attackTime = 20000;
    spec.decayTime = -1;
    Rig rig({ spec });
    SoundState* state = rig.engine->PlaySfx(1);

    Peaks centered;
    for (int i = 0; i < 4; i++) {
        centered = rig.Step();
    }
    Check(centered.left > 0 && centered.right > 0, "audible on both sides at centre pan");

    rig.engine->Sfx().CreatePostEvent(state, kSndpPan, 0); // hard left
    Peaks panned;
    for (int i = 0; i < 4; i++) {
        panned = rig.Step();
    }
    Check(panned.left > centered.left, "the left channel gets louder panned left");
    Check(panned.right < panned.left / 20, "the right channel nearly vanishes panned left");

    rig.engine->Sfx().CreatePostEvent(state, kSndpPan, 127); // hard right
    for (int i = 0; i < 4; i++) {
        panned = rig.Step();
    }
    Check(panned.left < panned.right / 20, "the left channel nearly vanishes panned right");
}

void TestUnloopedWaveGoesSilentAtItsEnd() {
    SoundSpec spec;
    spec.frames = 100; // 1600 samples: 2.2 output frames at unity pitch
    spec.attackTime = 20000;
    spec.decayTime = -1; // holds until released
    Rig rig({ spec });
    rig.engine->PlaySfx(1);

    std::vector<bool> audible;
    for (int i = 0; i < 8; i++) {
        audible.push_back(!rig.Step().Silent());
    }
    Check(audible[0] && audible[1], "plays through its first frames");
    Check(!audible[5] && !audible[6] && !audible[7], "silent once the wave's data has ended");
    Check(rig.engine->Sfx().ActiveStates() == 1, "the state lives on until its envelope ends");
}

void TestLoopedWaveKeepsPlaying() {
    SoundSpec spec;
    spec.frames = 8; // 128 samples
    spec.looped = true;
    spec.attackTime = 20000;
    spec.decayTime = -1; // holds until released
    Rig rig({ spec });
    rig.engine->PlaySfx(1);

    bool allAudible = true;
    for (int i = 0; i < 10; i++) {
        allAudible = !rig.Step().Silent() && allAudible;
    }
    Check(allAudible, "a looped wave plays far past its length");
    const Peaks p = rig.Step();
    Check(Near(p.left, ExpectedLevel(spec, false), 0.03), "and holds its level around the loop");
}

void TestPitchEventScalesPlayingTime() {
    auto audibleFrames = [](float pitch) {
        SoundSpec spec;
        spec.frames = 1500; // 24000 samples: about 33 output frames at unity pitch
        spec.attackTime = 20000;
        spec.decayTime = -1; // holds until released
        Rig rig({ spec });
        SoundState* state = rig.engine->PlaySfx(1);
        SndEvent unused;
        (void)unused;
        union {
            float f;
            int32_t i;
        } bits;
        bits.f = pitch;
        rig.engine->Sfx().CreatePostEvent(state, kSndpPitch, bits.i);
        int count = 0;
        for (int i = 0; i < 80; i++) {
            if (!rig.Step().Silent()) {
                count++;
            }
        }
        return count;
    };
    const int unity = audibleFrames(1.0f);
    const int octave = audibleFrames(2.0f);
    Check(unity >= 31 && unity <= 35, "unity pitch plays the wave's length");
    Check(std::abs(octave * 2 - unity) <= 3, "an octave up plays it in half the time");
}

void TestSlotVolumeScalesLevel() {
    SoundSpec spec;
    spec.frames = 2000;
    spec.attackTime = 20000;
    spec.decayTime = -1; // holds until released
    Rig rig({ spec });
    rig.engine->PlaySfx(1);
    Peaks full;
    for (int i = 0; i < 5; i++) {
        full = rig.Step();
    }
    rig.engine->Sfx().SetSfxSlotVolume(0, 0x4000);
    Check(rig.engine->Sfx().GetSfxSlotNaturalVolume(0) == 0x4000, "the slot's natural volume is stored");
    Peaks half;
    for (int i = 0; i < 8; i++) {
        half = rig.Step();
    }
    // Volume maps through a square, so half the slot volume is a quarter of the gain.
    const double ratio = double(half.left) / double(full.left);
    Check(ratio > 0.2 && ratio < 0.3, "a half-volume slot plays at a quarter gain");
}

void TestDeactivateStopsSound() {
    SoundSpec spec;
    spec.frames = 2000;
    spec.attackTime = 20000;
    spec.decayTime = -1; // holds until released
    spec.releaseTime = 50000;
    Rig rig({ spec });
    SoundState* state = rig.engine->PlaySfx(1);
    for (int i = 0; i < 4; i++) {
        rig.Step();
    }
    Check(rig.engine->Sfx().ActiveStates() == 1, "playing before deactivation");
    rig.engine->Sfx().Deactivate(state);
    for (int i = 0; i < 6; i++) {
        rig.Step();
    }
    Check(rig.engine->Sfx().ActiveStates() == 0, "a deactivated sound is released and freed");
    Check(rig.engine->Sfx().AllocatedVoices() == 0, "its voice is gone");
    Check(rig.Step().Silent(), "silent after release");
}

void TestSoundIndexZeroAndOutOfRange() {
    SoundSpec spec;
    Rig rig({ spec });
    Check(rig.engine->PlaySfx(0) == nullptr, "sound 0 is no sound");
    Check(rig.engine->PlaySfx(2) == nullptr, "an index past the bank plays nothing");
    rig.engine->Sfx().SetBootswitchSound(true);
    Check(rig.engine->PlaySfx(1) == nullptr, "disabled sound plays nothing");
    Check(rig.Step().Silent(), "and renders silence");
}

void TestSoundIndexIsCountedFromOne() {
    SoundSpec quiet;
    quiet.nibble = 1;
    SoundSpec loud;
    loud.nibble = 7;
    loud.frames = 2000;
    Rig rig({ quiet, loud });
    rig.engine->PlaySfx(2);
    Peaks p;
    for (int i = 0; i < 5; i++) {
        p = rig.Step();
    }
    Check(Near(p.left, ExpectedLevel(loud, false), 0.03), "sound 2 is the bank's second sound");
}

void TestLoopedSoundOverTheLimitWaitsForAVoice() {
    SoundSpec spec;
    spec.frames = 8;
    spec.looped = true;
    spec.attackTime = 20000;
    spec.decayTime = -1; // holds until released
    EngineConfig config;
    config.sfx.maxSounds = 2;
    Rig rig({ spec }, config);

    SoundState* states[3];
    for (SoundState*& state : states) {
        state = rig.engine->PlaySfx(1);
    }
    for (int i = 0; i < 6; i++) {
        rig.Step();
    }
    Check(rig.engine->Sfx().AllocatedVoices() == 2, "no more voices than the player's limit");
    Check(states[2]->playingState == kSoundStateWaitVoice, "a looped sound over the limit keeps waiting");

    rig.engine->Sfx().Deactivate(states[0]);
    for (int i = 0; i < 6; i++) {
        rig.Step();
    }
    Check(states[2]->playingState == kSoundStatePlaying, "and starts once a voice frees up");
    Check(rig.engine->Sfx().AllocatedVoices() == 2, "still at the limit");
}

void TestOneShotOverTheLimitPreemptsAPlayingSound() {
    SoundSpec spec;
    spec.frames = 2000;
    spec.attackTime = 20000;
    spec.decayTime = 5000000;
    EngineConfig config;
    config.sfx.maxSounds = 2;
    Rig rig({ spec }, config);

    SoundState* states[3];
    for (SoundState*& state : states) {
        state = rig.engine->PlaySfx(1);
    }
    for (int i = 0; i < 8; i++) {
        rig.Step();
    }
    Check(states[2]->playingState == kSoundStatePlaying, "a one-shot over the limit starts once it preempts a sound");
    Check(rig.engine->Sfx().AllocatedVoices() == 2, "the preempted sound's voice was reused");
    Check(rig.engine->Sfx().ActiveStates() == 2, "the preempted sound was freed");
}

void TestStolenVoiceKeepsPlayingNewSound() {
    SoundSpec lowPriority;
    lowPriority.frames = 2000;
    lowPriority.looped = true;
    lowPriority.attackTime = 20000;
    lowPriority.decayTime = -1;
    lowPriority.nibble = 3;
    SoundSpec loud = lowPriority;
    loud.nibble = 7;

    EngineConfig config;
    config.maxPVoices = 1;
    Rig rig({ lowPriority, loud }, config);
    rig.engine->PlaySfx(1);
    for (int i = 0; i < 3; i++) {
        rig.Step();
    }
    rig.engine->PlaySfx(2);
    Peaks p;
    for (int i = 0; i < 6; i++) {
        p = rig.Step();
    }
    Check(Near(p.left, ExpectedLevel(loud, false), 0.05), "the new sound takes over the only voice");
}

void TestCompositeSoundStartsPartsAtTheirOwnTimes() {
    SoundSpec first;
    first.frames = 2000;
    first.attackTime = 20000;
    first.decayTime = 5000000;
    first.velocityMin = 2; // next sound
    first.velocityMax = 0;
    SoundSpec second = first;
    second.velocityMin = 0;
    second.velocityMax = 3; // starts about 100 ms later
    second.nibble = 2;
    Rig rig({ first, second });
    rig.engine->PlaySfx(1);
    Check(rig.engine->Sfx().ActiveStates() == 2, "both parts of a composite sound get states");
    rig.Step();
    Check(rig.engine->Sfx().AllocatedVoices() == 1, "only the first part has started");
    for (int i = 0; i < 4; i++) {
        rig.Step();
    }
    Check(rig.engine->Sfx().AllocatedVoices() == 2, "the second part starts later");
}

void TestStatesAreReusedAfterManySounds() {
    SoundSpec spec;
    spec.frames = 4;
    spec.attackTime = 1000;
    spec.decayTime = 1000;
    spec.releaseTime = 1000;
    EngineConfig config;
    config.sfx.stateCount = 4;
    Rig rig({ spec }, config);
    for (int round = 0; round < 20; round++) {
        Check(rig.engine->PlaySfx(1) != nullptr, "a state is free each round");
        for (int i = 0; i < 4; i++) {
            rig.Step();
        }
    }
    Check(rig.engine->Sfx().ActiveStates() == 0, "all states returned to the pool");
}

} // namespace

int main() {
    TestSoundReachesItsLevelAndEnds();
    TestAttackRampsUp();
    TestPanEventMovesSoundLeft();
    TestUnloopedWaveGoesSilentAtItsEnd();
    TestLoopedWaveKeepsPlaying();
    TestPitchEventScalesPlayingTime();
    TestSlotVolumeScalesLevel();
    TestDeactivateStopsSound();
    TestSoundIndexZeroAndOutOfRange();
    TestSoundIndexIsCountedFromOne();
    TestLoopedSoundOverTheLimitWaitsForAVoice();
    TestOneShotOverTheLimitPreemptsAPlayingSound();
    TestStolenVoiceKeepsPlayingNewSound();
    TestCompositeSoundStartsPartsAtTheirOwnTimes();
    TestStatesAreReusedAfterManySounds();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::puts("AudioDriverTest passed");
    return 0;
}
