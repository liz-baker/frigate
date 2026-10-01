#include "level/StageAi.h"
#include "level/StageGun.h"

namespace Frigate::Level {

void Stage::GunBridge::GuardsHeardShot(float noise) {
    // bondviewGetVisibleToGuardsFlag.
    if (stage_.ai_ != nullptr && stage_.aiBridge_->Player().visibleToGuards) {
        stage_.ai_->GuardsHeardSound(noise);
    }
}

void Stage::GunBridge::ItemUsedOnProp() {
    if (!stage_.setup_ || stage_.player_.PropId() < 0) {
        return;
    }
    const std::vector<int>& farFirst = stage_.render_.OnScreenProps();
    const std::vector<int> nearFirst(farFirst.rbegin(), farFirst.rend());
    const int prop = Interact::Find(stage_.MakeInteractScene(), stage_.MakeInteractPlayer(), nearFirst);
    if (prop < 0) {
        return;
    }
    if (PlacedObject* object = stage_.objects_.ForProp(prop)) {
        object->state |= ObjectState::Activated;
    }
}

void Stage::GunBridge::SetFadeColour(int red, int green, int blue, float frac) {
    stage_.colour_.SetColour(red, green, blue, frac);
}

void Stage::GunBridge::AdjustFade(float time, int red, int green, int blue, float frac) {
    stage_.colour_.Adjust(time, red, green, blue, frac);
}

} // namespace Frigate::Level
