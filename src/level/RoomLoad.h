#pragma once

#include "level/BgLevel.h"
#include "level/Fog.h"
#include "level/LightFixtures.h"

namespace Frigate::Level {

// bgLoadRoomModelData's fog and light fixture work: the stage's light fixture
// tables, and patching a room's lists for the stage's fog state as the room
// loads.
class RoomLoad final : public RoomObserver {
  public:
    RoomLoad(const Fog& fog, LightFixtures& fixtures) : fog_(fog), fixtures_(fixtures) {
    }

    void StageLoading() override;
    void RoomLoaded(int room, RoomGeometry& geometry) override;

  private:
    const Fog& fog_;
    LightFixtures& fixtures_;
};

} // namespace Frigate::Level
