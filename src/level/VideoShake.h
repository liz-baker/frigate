#pragma once

#include <cstdint>

namespace Frigate::Level {

// The video interface's vertical shake (viShake and the vStart arithmetic in
// viVsyncRelated): the whole picture moves up and down by `Intensity()` lines,
// the direction swapping every retrace, until ten retraces pass without another
// viShake.
class VideoShake {
  public:
    // viShake.
    void Shake(float intensity) {
        if (intensity > 14.0f) {
            intensity = 14.0f;
        }
        if (intensity < 0.0f) {
            intensity = 0.0f;
        }
        intensity_ = static_cast<int32_t>(intensity);
        timer_ = 10;
    }

    // `retraces` vertical retraces have passed since the last call. The timer
    // runs down once per retrace. The picture's direction swaps once per call,
    // not once per retrace: a frame lasting an even number of retraces would
    // otherwise always be drawn the same way and never shake. Returns the
    // offset in lines to draw the next frame at, positive down.
    int32_t Advance(int32_t retraces) {
        for (int32_t i = 0; i < retraces && timer_ > 0; i++) {
            timer_--;
            if (timer_ == 0) {
                intensity_ = 0;
            }
        }
        offset_ = sign_ * intensity_;
        sign_ = -sign_;
        return offset_;
    }

    int32_t Intensity() const {
        return intensity_;
    }
    // What the last Advance returned.
    int32_t Offset() const {
        return offset_;
    }

  private:
    int32_t intensity_ = 0;
    uint32_t timer_ = 0;
    int32_t sign_ = 1;
    int32_t offset_ = 0;
};

} // namespace Frigate::Level
