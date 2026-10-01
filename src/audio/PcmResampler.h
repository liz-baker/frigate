#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Converts interleaved stereo S16 audio between sample rates by linear
// interpolation, carrying its position across calls so consecutive chunks
// join without a seam. The synth runs at the N64's 22 kHz; the output device
// runs at whatever rate libultraship opened it at.
namespace Frigate::Audio {

class PcmResampler {
  public:
    PcmResampler(int inputRate, int outputRate);

    // Appends the resampled `inputFrames` stereo frames to `out`.
    void Process(const int16_t* in, size_t inputFrames, std::vector<int16_t>* out);

  private:
    double mStep;
    // Position of the next output frame, in input frames after the previous
    // chunk's last frame.
    double mPhase = 0.0;
    int16_t mPrevious[2] = { 0, 0 };
};

} // namespace Frigate::Audio
