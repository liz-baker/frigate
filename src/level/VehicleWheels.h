#pragma once

#include <array>

#include "level/Matrix.h"
#include "level/ModelHandle.h"
#include "level/Objects.h"

namespace Frigate::Level {

// The wheel spin and steering the vehicle branch of objTick (propobj.c) works
// out as the vehicle is drawn. `isOwner` is isSimOwner: the owner's spin is
// applied twice, once inside the owner branch and once after it.
void StepVehicleWheels(ObjectAi& ai, const VehicleGeometry& geometry, float scale, float delta, bool isOwner);

// The matrices of the wheels in switch slots 1 to 4 (indices 1 to 4; index 0 is
// `base`): the front wheels (slots 1 and 2) turn about Y as well.
std::array<Mtxf, 5> VehicleWheelPose(const VehicleGeometry& geometry, const ObjectAi& ai, const Mtxf& base);

} // namespace Frigate::Level
