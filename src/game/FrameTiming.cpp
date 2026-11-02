#include "game/FrameTiming.h"

namespace Frigate::Game {

int TicksPerFrame(FramePhase phase) {
    // TODO(https://github.com/liz-baker/frigate/issues/112): every value here
    // other than the default is hand-set from hardware observation, not
    // derived; replace them with measured ones and note each row's source.
    switch (phase) {
        case FramePhase::Default:
            return 1;
        case FramePhase::EyeIntroSweep:
            return 1;
        case FramePhase::EyeIntroSight:
            return 1;
        case FramePhase::EyeIntroBond:
            return 2;
    }
    return 1;
}

int TickMeter::Ticks(Clock::time_point now) {
    int ticks = 1;
    if (last_) {
        const double seconds = std::chrono::duration<double>(now - *last_).count();
        const int elapsed = static_cast<int>(seconds * kTicksPerSecond + 0.5);
        ticks = elapsed < 1 ? 1 : (elapsed > kMaxTicks ? kMaxTicks : elapsed);
    }
    last_ = now;
    return ticks;
}

} // namespace Frigate::Game
