#pragma once

#include <atomic>
#include <memory>
#include <thread>

namespace Ship {
class Context;
} // namespace Ship

namespace Frigate {
class AudioBankResource;
class MusicSequencesResource;
} // namespace Frigate

namespace Frigate::AudioDriver {
class AudioEngine;
} // namespace Frigate::AudioDriver

namespace Frigate::Game {

// The game's audio, running: the AudioEngine built from the audio archive, made
// the one Sound's calls use, and a thread that renders frames to the audio
// output at the rate it plays them. The original's audio runs on its own thread
// too, so music carries on while the game loads a stage.
class SoundOutput {
  public:
    // Null, with the reason logged, when there is no audio output or the loaded
    // archives hold no audio: the game then runs silent.
    static std::unique_ptr<SoundOutput> Start(const std::shared_ptr<Ship::Context>& context);
    ~SoundOutput();
    SoundOutput(const SoundOutput&) = delete;
    SoundOutput& operator=(const SoundOutput&) = delete;

  private:
    SoundOutput() = default;

    std::shared_ptr<AudioBankResource> mSfxBank;
    std::shared_ptr<AudioBankResource> mInstruments;
    std::shared_ptr<MusicSequencesResource> mSequences;
    std::unique_ptr<AudioDriver::AudioEngine> mEngine;
    std::atomic<bool> mStop{ false };
    std::thread mThread;
};

} // namespace Frigate::Game
