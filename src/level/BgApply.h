#pragma once

#include <cstddef>

#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// The tables bgApplyDynamicCCRMLUT patches a room's lists with
// (ptrDynamic_CC_RM_LUT): each is a run of command pairs, a command to look for
// and the command to put in its place. Only the four a room load uses are here;
// the game's table has no caller for its other entries.
enum class RoomLut {
    // DL_LUT_PRIMARY_ADDFOG and DL_LUT_SECONDARY_ADDFOG: a stage with fog
    // blends what it draws into the fog colour.
    PrimaryAddFog,
    SecondaryAddFog,
    // DL_LUT_PRIMARY and DL_LUT_SECONDARY: a stage without fog takes its alpha
    // from the environment colour rather than from the vertices, which carry
    // fog factors while fog is on.
    Primary,
    Secondary,
};

// bgApplyDynamicCCRMLUT: replaces each of the `count` commands that matches the
// first of a pair in `lut` with the pair's second, trying the pairs in order
// (a replaced command is tested against the pairs after it).
void ApplyDynamicCCRMLUT(Gfx* commands, size_t count, RoomLut lut);

} // namespace Frigate::Level
