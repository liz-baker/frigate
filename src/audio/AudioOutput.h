#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "PcmResampler.h"

namespace Ship {
class Audio;
class AudioPlayer;
} // namespace Ship

namespace Frigate::Audio {

// Feeds the synth's frames to the audio output at the rate it plays them: the
// caller renders a frame when WantsFrame says the output has room, and hands it
// to Submit.
class AudioOutput {
  public:
    // `inputRate` is the rate of the frames Submit takes.
    AudioOutput(const std::shared_ptr<Ship::Audio>& audio, const std::shared_ptr<Ship::AudioPlayer>& player,
                int inputRate);

    // Whether the output wants another frame now; sleeps briefly when not.
    bool WantsFrame();
    // Queues `frames` interleaved stereo frames.
    void Submit(const int16_t* frame, size_t frames);
    // Lets what is queued play out.
    void Drain();

  private:
    std::shared_ptr<Ship::Audio> mAudio;
    std::shared_ptr<Ship::AudioPlayer> mPlayer;
    PcmResampler mResampler;
    std::vector<int16_t> mPending;
    int mDesired;
};

} // namespace Frigate::Audio
