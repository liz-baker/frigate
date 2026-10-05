#pragma once

#include <functional>
#include <map>
#include <string>

#include "level/ChrActions.h"
#include "level/ChrAi.h"
#include "level/LevelInfo.h"
#include "level/ModelHandle.h"

namespace Frigate::Level {

// The vehicle and aircraft branches of objTick (propobj.c): the object's AI
// list, its speed ramps, and a vehicle's steering and drive along the patrol
// path the list started.
class Vehicles {
  public:
    Vehicles(Objects& objects, Stan& stan, const Waypoints& paths, Chrs& chrs, ChrAi& ai, ChrActions& actions,
             const StageSetup& setup, ModelFactory* models, LevelId stage)
        : objects_(objects), stan_(stan), paths_(paths), chrs_(chrs), ai_(ai), actions_(actions), setup_(setup),
          models_(models), stage_(stage) {
    }

    // objTick for a vehicle or aircraft; other objects are left alone.
    void Tick(PlacedObject& object);
    // The end of objTick for an aircraft: its rotor turns while it is on screen.
    void Pose(PlacedObject& object, bool onScreen);
    // lvlGetControlsLockedFlag, which keeps an engine from starting.
    void SetControlsLockedCheck(std::function<bool()> locked) {
        controlsLocked_ = std::move(locked);
    }

    // detonate_proxmine_In_range, for the mines a vehicle drives over.
    void SetMineDetonator(std::function<void(const Vec3&)> detonate) {
        detonateMines_ = std::move(detonate);
    }

  private:
    void TickVehicle(PlacedObject& object);
    void TickAircraft(PlacedObject& object);
    // The vehicle's geometry, loaded once per model; null when it has none.
    const VehicleGeometry* Geometry(const PlacedObject& object);
    // sub_GAME_7F044B38: sits the vehicle on the ground at its heading, tilted
    // to the four points it rests on. False when a point is off the tiles.
    bool Settle(PlacedObject& object);
    // The ramp speed, speedaim and speedtime60 share.
    void RampSpeed(PlacedObject& object, float delta);
    // The tail of a vehicle's and an aircraft's tick: its engine plays at
    // `volume` (what sub_GAME_7F053894 gave for its position, 0 for none) as
    // `sfx`, or stops.
    void TickEngineSound(PlacedObject& object, int16_t sfx, int32_t volume);

    Objects& objects_;
    Stan& stan_;
    const Waypoints& paths_;
    Chrs& chrs_;
    ChrAi& ai_;
    ChrActions& actions_;
    const StageSetup& setup_;
    ModelFactory* models_;
    LevelId stage_;
    std::function<bool()> controlsLocked_;
    std::function<void(const Vec3&)> detonateMines_;
    std::map<std::string, std::optional<VehicleGeometry>> geometry_;
};

} // namespace Frigate::Level
