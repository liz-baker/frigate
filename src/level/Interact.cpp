#include "level/Interact.h"

#include <cmath>

#include "level/PickupText.h"
#include "level/Stan.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;

// PROPOBJ_STR_40: what a door says when the player lacks its key.
constexpr int32_t kDoorLockedText = StringId(TextBank::PropObj, PropObjText::DoorLocked);

struct Interaction {
    const InteractScene& scene;
    const InteractPlayer& player;
    int found = -1;
};

// The angle from `yaw` to the direction of (x, z), wrapped the way the door
// tests wrap it: -pi to pi.
float SignedAngleFrom(float x, float z, float yaw) {
    const float angle = GameAtan2f(x, z);
    float diff = angle - yaw;
    if (angle < yaw) {
        diff += kTau;
    }
    if (diff > kPi) {
        diff -= kTau;
    }
    return diff;
}

// chrpropTestPointInPaddedBoundPad.
bool PointInPaddedBoundPad(const Vec3& position, float radius, const BoundPad& bound) {
    const Pad& pad = bound.pad;
    const auto& box = bound.box;
    const float dx = position[0] - pad.position[0];
    const float dy = position[1] - pad.position[1];
    const float dz = position[2] - pad.position[2];
    const Vec3 side = { (pad.up[1] * pad.look[2]) - (pad.look[1] * pad.up[2]),
                        (pad.up[2] * pad.look[0]) - (pad.look[2] * pad.up[0]),
                        (pad.up[0] * pad.look[1]) - (pad.look[0] * pad.up[1]) };

    float d = (pad.look[2] * dz) + ((dx * pad.look[0]) + (dy * pad.look[1]));
    if (box[5] + radius < d || d < box[4] - radius) {
        return false;
    }
    d = (pad.up[2] * dz) + ((dx * pad.up[0]) + (dy * pad.up[1]));
    if (box[3] + radius < d || d < box[2] - radius) {
        return false;
    }
    d = (dx * side[0]) + (dy * side[1]) + (side[2] * dz);
    return !(box[1] + radius < d || d < box[0] - radius);
}

// door7F05522C: the angles, relative to where the player looks, of the two
// edges of the door's opening (`low` first): the near edge and the far one as
// the door stands.
void DoorEdgeAngles(const PlacedDoor& door, const InteractPlayer& player, bool altCoordSystem, float* low,
                    float* high) {
    const Pad& pad = door.bound.pad;
    const auto& box = door.bound.box;
    constexpr float kReach = 30.0f * 0.75f;
    const float playerX = (player.look[0] * kReach) + player.position[0];
    const float playerZ = (player.look[2] * kReach) + player.position[2];

    float minimum;
    float maximum;
    Vec3 normal;
    if (altCoordSystem) {
        minimum = box[0];
        maximum = box[1];
        normal = { (pad.up[1] * pad.look[2]) - (pad.look[1] * pad.up[2]),
                   (pad.up[2] * pad.look[0]) - (pad.look[2] * pad.up[0]),
                   (pad.up[0] * pad.look[1]) - (pad.look[0] * pad.up[1]) };
    } else {
        minimum = box[2];
        maximum = box[3];
        normal = pad.up;
    }

    const float nearDiff = SignedAngleFrom((pad.position[0] + (normal[0] * minimum)) - playerX,
                                           (pad.position[2] + (normal[2] * minimum)) - playerZ, player.yaw);
    float farDiff;
    if (door.doorType == DoorMotionType::Swinging) {
        float swing = (door.openPosition * kTau) / 360.0f;
        if ((door.flags & ObjectFlag::DoorOpenToFront) != 0) {
            swing = kTau - swing;
        }
        const float cosine = GameCosf(swing);
        const float sine = GameSinf(swing);
        const float width = maximum - minimum;
        farDiff = SignedAngleFrom(
            ((pad.position[0] + (normal[0] * minimum)) + (width * ((normal[0] * cosine) + (normal[2] * sine)))) -
                playerX,
            ((pad.position[2] + (normal[2] * minimum)) + (width * (((-normal[0]) * sine) + (normal[2] * cosine)))) -
                playerZ,
            player.yaw);
    } else {
        farDiff = SignedAngleFrom((pad.position[0] + (normal[0] * maximum)) - playerX,
                                  (pad.position[2] + (normal[2] * maximum)) - playerZ, player.yaw);
    }

    if (nearDiff < farDiff) {
        *low = nearDiff;
        *high = farDiff;
    } else {
        *low = farDiff;
        *high = nearDiff;
    }
}

// door7F0555F8: whether the player looks at the door (or at the opening its
// siblings make together); true to keep looking at the props behind it.
bool TestDoorLooking(Interaction* interaction, int doorIndex, bool altCoordSystem) {
    constexpr float kLimit = 0.34906587f;
    if (interaction->found >= 0) {
        return true;
    }
    Doors& doors = *interaction->scene.doors;
    const PlacedDoor& door = doors.All()[static_cast<size_t>(doorIndex)];
    float low;
    float high;
    DoorEdgeAngles(door, interaction->player, altCoordSystem, &low, &high);

    if (low >= -kLimit && low <= kLimit && high >= -kLimit && high <= kLimit) {
        interaction->found = door.prop;
        return false;
    }

    int sibling = door.linkedDoor;
    while (sibling >= 0 && sibling != doorIndex && (low >= 0.0f || high < 0.0f)) {
        float siblingLow;
        float siblingHigh;
        DoorEdgeAngles(doors.All()[static_cast<size_t>(sibling)], interaction->player, altCoordSystem, &siblingLow,
                       &siblingHigh);
        if (low > 0.0f && siblingLow < low) {
            low = siblingLow;
        }
        if (high < 0.0f && high < siblingHigh) {
            high = siblingHigh;
        }
        sibling = doors.All()[static_cast<size_t>(sibling)].linkedDoor;
    }

    if ((high - low) < kPi && low < 0.0f && high > 0.0f) {
        interaction->found = door.prop;
        return false;
    }
    return true;
}

// doorTestForInteract.
bool TestDoor(Interaction* interaction, int prop) {
    const InteractScene& scene = interaction->scene;
    const PropList& props = scene.objects->Props();
    const int doorIndex = props[prop].object;
    if (doorIndex < 0) {
        return true;
    }
    const PlacedDoor& door = scene.doors->All()[static_cast<size_t>(doorIndex)];
    if ((door.flags & ObjectFlag::CannotActivate) != 0 || !(door.maxFrac > 0.0f) ||
        (props[prop].flags & PropFlag::OnScreen) == 0) {
        return true;
    }

    const InteractPlayer& player = interaction->player;
    bool maybe = false;
    const float dx = door.centre[0] - player.position[0];
    const float dy = door.centre[1] - player.position[1];
    const float dz = door.centre[2] - player.position[2];
    if (dx * dx + dz * dz < 40000.0f && dy < 200.0f && dy > -200.0f) {
        maybe = true;
    } else {
        // Does the door share a room with the player (chraiGetPropRoomIds)?
        const int playerRoom = player.tile != kNoTile ? scene.stan->Tile(player.tile).room : -1;
        bool shared = false;
        for (const uint8_t room : props[prop].rooms) {
            if (room == kNoRoom) {
                break;
            }
            if (room == playerRoom) {
                shared = true;
                break;
            }
        }
        if (shared && PointInPaddedBoundPad(player.position, 150.0f, door.bound)) {
            maybe = true;
        }
    }

    if (!maybe) {
        return true;
    }
    bool checkMore = TestDoorLooking(interaction, doorIndex, false);
    if (checkMore && (door.flags2 & ObjectFlag2::DoorAltCoordSystem) != 0) {
        checkMore = TestDoorLooking(interaction, doorIndex, true);
    }
    return checkMore;
}

// objTestForInteract.
void TestObject(Interaction* interaction, int prop) {
    const InteractScene& scene = interaction->scene;
    const PropList& props = scene.objects->Props();
    const PlacedObject* object = scene.objects->ForProp(prop);
    if (object == nullptr) {
        return;
    }
    // The alarm is always usable (the decomp compares the propdef type against
    // the value that names PROP_TYPE_PLAYER, 5, which is also PROPDEF_ALARM).
    const bool usable = object->type == static_cast<uint8_t>(PropDefType::Alarm) ||
                        (object->flags & ObjectFlag::CollectByInteractOnly) != 0 ||
                        (object->runtimeFlags &
                         (ObjectRuntime::LinkedToSwitch | ObjectRuntime::Unnamed0x2 | ObjectRuntime::Tagged)) != 0;
    if (!usable || (props[prop].flags & PropFlag::OnScreen) == 0 || !Objects::IsHealthy(*object) ||
        (object->flags & ObjectFlag::CannotActivate) != 0) {
        return;
    }

    const InteractPlayer& player = interaction->player;
    const float dx = object->runtimePosition[0] - player.position[0];
    const float dy = object->runtimePosition[1] - player.position[1];
    const float dz = object->runtimePosition[2] - player.position[2];

    float heightLimit = 200.0f;
    float distanceSquared = 40000.0f;
    float angleLimit = 0.3926991f;
    if (object->type == static_cast<uint8_t>(PropDefType::Aircraft) &&
        (object->flags & ObjectFlag::DoorOpenToFront) != 0) {
        heightLimit = 400.0f;
        distanceSquared = 160000.0f;
        angleLimit = 2.0943952f;
    }
    if (!(dx * dx + dz * dz < distanceSquared && dy < heightLimit && -heightLimit < dy)) {
        return;
    }

    const float angle = GameAtan2f(dx, dz);
    float diff = angle - player.yaw;
    if (angle < player.yaw) {
        diff += kTau;
    }
    if (diff > kPi) {
        diff = kTau - diff;
    }
    if (diff > angleLimit) {
        return;
    }
    if ((object->flags2 & ObjectFlag2::InteractCheckLos) == 0) {
        interaction->found = prop;
        return;
    }
    int tile = player.tile;
    if (tile != kNoTile && scene.stan->WalkTiles(&tile, player.position[0], player.position[2], props[prop].position[0],
                                                 props[prop].position[2])) {
        interaction->found = prop;
    }
}

} // namespace

int Interact::Find(const InteractScene& scene, const InteractPlayer& player,
                   const std::vector<int>& onScreenNearFirst) {
    Interaction interaction{ scene, player };
    const PropList& props = scene.objects->Props();
    for (const int prop : onScreenNearFirst) {
        const PropType type = props[prop].type;
        bool checkMore = true;
        if (type == PropType::Obj || type == PropType::Weapon) {
            TestObject(&interaction, prop);
        } else if (type == PropType::Door) {
            checkMore = TestDoor(&interaction, prop);
        }
        if (!checkMore) {
            break;
        }
    }
    return interaction.found;
}

void Interact::UseDoor(const InteractScene& scene, const InteractPlayer& player, int prop, const Inventory& inventory,
                       const Lang& lang, std::string* message) {
    Doors& doors = *scene.doors;
    PlacedDoor* found = doors.ForProp(prop);
    if (found == nullptr) {
        return;
    }
    PlacedDoor& door = *found;
    const int doorIndex = static_cast<int>(found - doors.All().data());
    const auto keyFlagsOf = [&scene](int64_t def) -> uint32_t {
        const PropDef& record = scene.setup->PropDefs()[static_cast<size_t>(def)];
        return record.Is(PropDefType::Key) ? record.keyFlags : 0;
    };

    bool canOpen = false;
    if (door.keyFlags == 0) {
        canOpen = true;
    } else if (inventory.HasKeyFlags(door.keyFlags, keyFlagsOf)) {
        canOpen = true;
    } else if (doors.IsInFront(door, player.position)) {
        if ((door.flags2 & ObjectFlag2::OneWayLockBack) != 0 && (door.flags2 & ObjectFlag2::OneWayLockFront) == 0) {
            canOpen = true;
        }
    } else if ((door.flags2 & ObjectFlag2::OneWayLockBack) == 0 && (door.flags2 & ObjectFlag2::OneWayLockFront) != 0) {
        canOpen = true;
    }

    if (!doors.IsPadlockFree(door)) {
        canOpen = false;
    }

    if (canOpen) {
        doors.ChooseSwingDirection(doorIndex, player.position);
        doors.ActivateWrapper(doorIndex);
    } else if (door.state == DoorState::Stationary && door.openPosition < 0.5f) {
        if ((door.flags2 & ObjectFlag2::DoorNoInteractText) == 0 && message != nullptr) {
            const TextOverride* text = inventory.TextByObject(static_cast<int64_t>(door.def));
            if (text != nullptr && text->pickupText != 0) {
                *message = std::string(lang.Get(text->pickupText));
            } else {
                *message = std::string(lang.Get(kDoorLockedText));
            }
        }
        door.runtimeFlags |= ObjectRuntime::Activated;
        door.flags2 |= ObjectFlag2::DoorTriedLocked;
    }
}

} // namespace Frigate::Level
