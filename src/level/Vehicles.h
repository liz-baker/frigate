#pragma once

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

    Objects& objects_;
    Stan& stan_;
    const Waypoints& paths_;
    Chrs& chrs_;
    ChrAi& ai_;
    ChrActions& actions_;
    const StageSetup& setup_;
    ModelFactory* models_;
    LevelId stage_;
    std::map<std::string, std::optional<VehicleGeometry>> geometry_;
};

} // namespace Frigate::Level
