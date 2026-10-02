#pragma once

#include <array>

#include "level/Matrix.h"
#include "level/ModelHandle.h"

namespace Frigate::Level {

// The matrices an aircraft's rotor is posed with (propobj.c's PROPDEF_AIRCRAFT
// pose): slot 2 is the main rotor, which turns about the model's y axis (about
// z when the aircraft is in motion), 3 the tail rotor, which turns about x, and
// 4 a part that follows the body.
struct RotorPose {
    std::array<Mtxf, 5> matrix{};
    std::array<bool, 5> posed{};
};

// `body` is the matrix of the aircraft's body (the one its animation gives
// the first node, or the placed matrix without an animation).
RotorPose PoseRotor(const SwitchGeometry& geometry, float angle, bool inMotion, const Mtxf& body);

} // namespace Frigate::Level
