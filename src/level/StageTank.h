#pragma once

#include "level/PlayerMove.h"
#include "level/Stage.h"

namespace Frigate::Level {

// What the player's tank reaches the stage through: the tank object and its
// model, the items the player takes in and out, and the objects and characters
// the tank runs into.
class Stage::TankBridge final : public TankHooks {
  public:
    explicit TankBridge(Stage& stage) : stage_(stage) {
    }

    PlacedObject* TankOf(int prop) override;
    const TankGeometry* Geometry(const PlacedObject& tank) override;
    const ModelBounds* Hull(const PlacedObject& tank) override;
    bool CollisionBounds(int prop, PropCollider* out) override;
    void SetCollides(int prop, bool collides) override;
    void EnterTank(int32_t ammo) override;
    int32_t LeaveTank() override;
    int TickTank(const Frame& frame) override;

  private:
    Stage& stage_;
};

} // namespace Frigate::Level
