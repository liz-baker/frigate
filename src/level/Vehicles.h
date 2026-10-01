#pragma once

#include <map>
#include <string>

#include "level/ChrAi.h"
#include "level/ModelHandle.h"

namespace Frigate::Level {

// The vehicle and aircraft branches of objTick (propobj.c): the object's AI
// list, its speed ramps, and a vehicle's steering and drive along the patrol
// path the list started.
class Vehicles {
  public:
    Vehicles(Objects& objects, Stan& stan, const Waypoints& paths, Chrs& chrs, ChrAi& ai, ModelFactory* models)
        : objects_(objects), stan_(stan), paths_(paths), chrs_(chrs), ai_(ai), models_(models) {
    }

    // objTick for a vehicle or aircraft; other objects are left alone.
    void Tick(PlacedObject& object);

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
    ModelFactory* models_;
    std::map<std::string, std::optional<VehicleGeometry>> geometry_;
};

} // namespace Frigate::Level
