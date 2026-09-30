#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "level/Doors.h"
#include "level/Inventory.h"
#include "level/Lang.h"
#include "level/Objects.h"
#include "level/StageSetup.h"

namespace Frigate::Level {

class Stan;

// What the use-button code reads of the player (getCurrentPlayerProp,
// bondviewGetPlayerYawRadians and field_488.theta_transform).
struct InteractPlayer {
    int prop = -1;
    Vec3 position{};
    int tile = kNoTile;
    // The look heading in radians, and the horizontal look direction.
    float yaw = 0;
    Vec3 look{};
};

// What finding the prop to use needs of the stage.
struct InteractScene {
    const StageSetup* setup = nullptr;
    Stan* stan = nullptr;
    Objects* objects = nullptr;
    Doors* doors = nullptr;
};

// The player's use button on doors and objects (chrprop.c propFindForInteract,
// propobj.c propdoorInteract and their helpers).
class Interact final {
  public:
    // propFindForInteract: the prop the player would use, from the props on
    // screen nearest first (`onScreenNearFirst`); -1 for none.
    static int Find(const InteractScene& scene, const InteractPlayer& player,
                    const std::vector<int>& onScreenNearFirst);

    // propdoorInteract: opens or closes the door of `prop`, or, when its key is
    // missing, says so (hudmsgBottomShow) and marks it tried. `message` gets the
    // text that is shown, when there is one.
    static void UseDoor(const InteractScene& scene, const InteractPlayer& player, int prop, const Inventory& inventory,
                        const Lang& lang, std::string* message);
};

} // namespace Frigate::Level
