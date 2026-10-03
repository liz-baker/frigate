#include "AudioEngine.h"

namespace Frigate::AudioDriver {

namespace {

// The game's effects table: 6 delay sections over a 160 ms line. One
// millisecond is 40 samples in these tables.
SynConfig MakeSynConfig(const EngineConfig& config) {
    constexpr int32_t ms = 40;
    SynConfig syn;
    syn.maxPVoices = config.maxPVoices;
    syn.maxUpdates = config.maxUpdates;
    syn.outputRate = kOutputRate;
    syn.fxType = kFxCustom;
    // clang-format off
    syn.fxParams = {
        6, 160 * ms,
        //  input    output  fbcoef  ffcoef    gain  rate  depth  filter
        0,       4 * ms,   9830,  -9830,       0,    0,     0,       0,
        4 * ms,  8 * ms,   9830,  -9830,  0x2B84,    0,     0,  0x2500,
        20 * ms, 64 * ms, 16384, -16384,  0x11EB,    0,     0,  0x3000,
        80 * ms, 140 * ms, 16384, -16384, 0x11EB,    0,     0,  0x3500,
        84 * ms, 120 * ms,  8192,  -8192,       0,    0,     0,  0x4000,
        0,       148 * ms, 13000, -13000,       0, 0x017C,   0xA,  0x4500,
    };
    // clang-format on
    return syn;
}

} // namespace

AudioEngine::AudioEngine(const Audio::AudioBank& sfxBank, const EngineConfig& config, const MusicAssets* music)
    : mSfxBank(sfxBank), mSynth(std::make_unique<Synth>(MakeSynConfig(config))) {
    // The music players join the synth before the sound-effect player, as in
    // the game; the synth runs the most recently added player first when two
    // are due together.
    if (music != nullptr && music->instruments != nullptr && music->sequences != nullptr) {
        mInstrumentBank = std::make_unique<LinkedBank>(*music->instruments);
        mMusic = std::make_unique<MusicPlayer>(mSynth.get(), *mInstrumentBank, *music->sequences, config.music);
    }
    mSfx = std::make_unique<SoundPlayer>(mSynth.get(), config.sfx);
}

void AudioEngine::RenderFrame(int16_t* out) {
    if (mMusic) {
        mMusic->Update();
    }
    mSynth->AudioFrame(out, kFrameSamples);
    // A music track waiting on a player to stop starts as soon as it has.
    if (mMusic) {
        mMusic->Update();
    }
}

} // namespace Frigate::AudioDriver
