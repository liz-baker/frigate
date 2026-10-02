#pragma once

#include "level/PlayerWeapons.h"
#include "level/Stage.h"

namespace Frigate::Level {

// What the gun's states reach the stage through: the guards hearing a shot and
// the items the hands use on the world.
class Stage::GunBridge final : public GunHooks {
  public:
    explicit GunBridge(Stage& stage) : stage_(stage) {
    }

    void GuardsHeardShot(float noise) override;
    void ItemUsedOnProp() override;
    void SetFadeColour(int red, int green, int blue, float frac) override;
    void AdjustFade(float time, int red, int green, int blue, float frac) override;
    void AnalyzeGoldenEyeKey() override;
    void GiveWeaponCaseItems() override;

  private:
    Stage& stage_;
};

} // namespace Frigate::Level
