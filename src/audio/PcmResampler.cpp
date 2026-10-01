#include "PcmResampler.h"

#include <cmath>

namespace Frigate::Audio {

PcmResampler::PcmResampler(int inputRate, int outputRate)
    : mStep(static_cast<double>(inputRate) / static_cast<double>(outputRate)) {
}

void PcmResampler::Process(const int16_t* in, size_t inputFrames, std::vector<int16_t>* out) {
    if (inputFrames == 0) {
        return;
    }

    // The stream is the previous chunk's last frame followed by this chunk:
    // frame i of it is `Frame(i)`.
    auto frame = [&](size_t index, int channel) -> int {
        return index == 0 ? mPrevious[channel] : in[(index - 1) * 2 + static_cast<size_t>(channel)];
    };

    double t = mPhase;
    while (true) {
        const size_t i = static_cast<size_t>(t);
        if (i + 1 > inputFrames) {
            break;
        }
        const double fraction = t - static_cast<double>(i);
        for (int channel = 0; channel < 2; channel++) {
            const int a = frame(i, channel);
            const int b = frame(i + 1, channel);
            out->push_back(static_cast<int16_t>(std::lround(a + (b - a) * fraction)));
        }
        t += mStep;
    }

    mPhase = t - static_cast<double>(inputFrames);
    mPrevious[0] = in[(inputFrames - 1) * 2];
    mPrevious[1] = in[(inputFrames - 1) * 2 + 1];
}

} // namespace Frigate::Audio
