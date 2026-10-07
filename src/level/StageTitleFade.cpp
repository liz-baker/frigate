#include "level/Stage.h"

namespace Frigate::Level {

void Stage::FadeToTitle() {
    if (cameraMode_ == CameraMode::FadeToTitle) {
        return;
    }
    cameraMode_ = CameraMode::FadeToTitle;
    // CHRAI_TICKRATE_F.
    constexpr float kFadeTicks = 60.0f;
    Cur().colour.SetColour(0, 0, 0, 0.0f);
    Cur().colour.SetFrac(kFadeTicks, 1.0f);
}

bool Stage::FadeToTitleComplete() const {
    return cameraMode_ == CameraMode::FadeToTitle && Cur().colour.IsComplete();
}

} // namespace Frigate::Level
