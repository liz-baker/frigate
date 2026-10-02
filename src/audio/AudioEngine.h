#pragma once

#include <cstdint>
#include <memory>

#include "AudioBank.h"
#include "AudioSynth.h"
#include "MusicPlayer.h"
#include "MusicSequences.h"
#include "SoundPlayer.h"

// The game's audio manager: the synth configured as GoldenEye configures it,
// the sound-effect player and the music players on top, and the fixed-size
// frames the output is produced in.
namespace Frigate::AudioDriver {

// The rate the synth runs at (the N64's DAC rate for a 22050 Hz request).
constexpr int kOutputRate = 22048;

// Stereo frames per call of RenderFrame: one 30 Hz field at the output rate,
// rounded up to a multiple of 16.
constexpr int kFrameSamples = 736;

struct EngineConfig {
    int maxPVoices = 0x18;
    int maxUpdates = 0x80;
    SoundPlayerConfig sfx;
    MusicPlayerConfig music;
};

// What the music players play from. Both must outlive the engine.
struct MusicAssets {
    // The instrument bank ("audio/instruments").
    const Audio::AudioBank* instruments = nullptr;
    // The sequence table ("audio/music").
    const Audio::MusicSequenceTable* sequences = nullptr;
};

class AudioEngine {
  public:
    // `sfxBank` is the sound-effect bank sounds are played from; it must
    // outlive the engine. With `music`, the three music players are created
    // too (musicSeqPlayerInit).
    AudioEngine(const Audio::AudioBank& sfxBank, const EngineConfig& config = EngineConfig(),
                const MusicAssets* music = nullptr);
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Produces kFrameSamples interleaved stereo frames (kFrameSamples * 2
    // samples) at kOutputRate.
    void RenderFrame(int16_t* out);

    // Starts sound-effect `soundIndex` (see SoundPlayer).
    SoundState* PlaySfx(int16_t soundIndex, SoundState* pending = nullptr) {
        return mSfx->PlaySfx(mSfxBank, soundIndex, pending);
    }
    SoundPlayer& Sfx() {
        return *mSfx;
    }
    // The music players; only when the engine was given music assets.
    bool HasMusic() const {
        return mMusic != nullptr;
    }
    MusicPlayer& Music() {
        return *mMusic;
    }
    const LinkedBank& SfxBank() const {
        return mSfxBank;
    }
    Synth& GetSynth() {
        return *mSynth;
    }

  private:
    LinkedBank mSfxBank;
    std::unique_ptr<Synth> mSynth;
    std::unique_ptr<LinkedBank> mInstrumentBank;
    std::unique_ptr<MusicPlayer> mMusic;
    std::unique_ptr<SoundPlayer> mSfx;
};

} // namespace Frigate::AudioDriver
