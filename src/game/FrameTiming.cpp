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

} // namespace Frigate::Game
