#include "level/Doors.h"

#include <cmath>

#include "level/ApplySpeed.h"
#include "level/BgLevel.h"
#include "level/Stan.h"

namespace Frigate::Level {

namespace {

constexpr float kMinimumScaleRatio = 0.000001f;
constexpr float kTau = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;

Mtxf IdentityWithPosition(const Vec3& position) {
    Mtxf out{};
    out.m[0][0] = out.m[1][1] = out.m[2][2] = out.m[3][3] = 1.0f;
    out.m[3][0] = position[0];
    out.m[3][1] = position[1];
    out.m[3][2] = position[2];
    return out;
}

// updateDoorDisplacement.
bool UpdateDisplacement(PlacedDoor* door, int32_t ticks) {
    bool moving = false;
    if (door->state == DoorState::Opening) {
        ChrObjApplySpeed(&door->openPosition, door->maxFrac, &door->speed, door->accel, door->decel, door->maxSpeed,
                         ticks);
        moving = true;
    } else if (door->state == DoorState::Closing) {
        ChrObjApplySpeed(&door->openPosition, 0.0f, &door->speed, door->accel, door->decel, door->maxSpeed, ticks);
        moving = true;
    }
    if (door->maxFrac <= door->openPosition) {
        door->openPosition = door->maxFrac;
    } else if (door->openPosition <= 0.0f) {
        door->openPosition = 0.0f;
    }
    return moving;
}

// sub_GAME_7F00324C: the rooms straddling a bound pad's plane, 50 units
// either side of the tile-walked pad centre, and those two probe points
// (for the portal lookup between them).
void RoomsAcrossPad(Stan& stan, const BoundPad& bound, int* room1, int* room2, Vec3* beyond, Vec3* before) {
    Vec3 centre = BoundPadCentre(bound);
    int tile = bound.pad.stanTile;
    if (!stan.WalkTiles(&tile, bound.pad.position[0], bound.pad.position[2], centre[0], centre[2])) {
        tile = bound.pad.stanTile;
        centre = bound.pad.position;
    }
    Vec3 normal = { (bound.pad.up[1] * bound.pad.look[2]) - (bound.pad.up[2] * bound.pad.look[1]),
                    (bound.pad.up[2] * bound.pad.look[0]) - (bound.pad.up[0] * bound.pad.look[2]),
                    (bound.pad.up[0] * bound.pad.look[1]) - (bound.pad.up[1] * bound.pad.look[0]) };
    const float invLength =
        1.0f / std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
    for (float& component : normal) {
        component *= invLength;
    }

    int beyondTile = tile;
    (*beyond)[0] = centre[0] + (normal[0] * 50.0f);
    (*beyond)[1] = centre[1];
    (*beyond)[2] = centre[2] + (normal[2] * 50.0f);
    stan.WalkTiles(&beyondTile, centre[0], centre[2], (*beyond)[0], (*beyond)[2]);
    *room1 = stan.TileRoom(beyondTile);

    int beforeTile = tile;
    (*before)[0] = centre[0] - (normal[0] * 50.0f);
    (*before)[1] = centre[1];
    (*before)[2] = centre[2] - (normal[2] * 50.0f);
    stan.WalkTiles(&beforeTile, centre[0], centre[2], (*before)[0], (*before)[2]);
    *room2 = stan.TileRoom(beforeTile);

    if (*room2 == *room1) {
        *room2 = -1;
    }
}

} // namespace

void Doors::Reset(Stan* stan, BgLevel* level, std::shared_ptr<const PropModels> models, PropList* props) {
    stan_ = stan;
    level_ = level;
    models_ = std::move(models);
    props_ = props;
    doors_.clear();
    byDef_.clear();
    activated_ = nullptr;
    padlocked_ = nullptr;
    clear_ = nullptr;
}

const PlacedDoor* Doors::ForDef(size_t def) const {
    return def < byDef_.size() && byDef_[def] >= 0 ? &doors_[static_cast<size_t>(byDef_[def])] : nullptr;
}

bool Doors::Create(const StageSetup& setup, size_t defIndex) {
    const PropDef& def = setup.PropDefs()[defIndex];
    if (!def.Is(PropDefType::Door) || !models_ || stan_ == nullptr) {
        return false;
    }
    const PropModel* model = models_->Model(def.model);
    if (model == nullptr || !model->bounds) {
        return false;
    }
    // setupDoor always indexes g_CurrentSetup.boundpads[door->pad] directly: a
    // door's pad field is a bound pad index, not a simple pad (Objects.cpp's
    // 10000-and-above encoding is for object records, not door records).
    if (def.pad < 0 || static_cast<size_t>(def.pad) >= setup.BoundPads().size()) {
        return false;
    }
    Pad pad = setup.BoundPads()[static_cast<size_t>(def.pad)].pad;
    std::array<float, 6> box = setup.BoundPads()[static_cast<size_t>(def.pad)].box;
    if (pad.stanTile == kNoTile) {
        return false;
    }

    int room1 = -1;
    int room2 = -1;
    int portalNumber = -1;
    if ((def.flags & (ObjectFlag::CullBehindDoor | ObjectFlag::NoPortalClose)) != 0) {
        Vec3 beyond{};
        Vec3 before{};
        RoomsAcrossPad(*stan_, setup.BoundPads()[static_cast<size_t>(def.pad)], &room1, &room2, &beyond, &before);
        if ((def.flags & ObjectFlag::CullBehindDoor) != 0 && room1 >= 0 && room2 >= 0 && level_ != nullptr) {
            portalNumber = level_->FindWindowPortal(beyond, before);
        }
    }

    // DOOR_SCALE: g_DoorScale reshapes the bound pad's box (and, across a
    // portal, nudges its position along the portal's normal) before the door
    // is fit to it. This mutates the setup's shared BoundPadRecord in the
    // decomp; ported here as a per-door local copy, since two doors sharing
    // one bound pad does not happen in practice.
    const float doorScale = setup.DoorScale();
    if (doorScale != 1.0f) {
        if (portalNumber >= 0 && level_ != nullptr) {
            PortalMetric metric = MeasurePortal(level_->Portals()[static_cast<size_t>(portalNumber)]);
            const float roomScale = (stan_->Scale() != 0.0f) ? 1.0f / stan_->Scale() : 1.0f;
            metric.min *= roomScale;
            float distance = (pad.position[0] * metric.normal[0]) + (pad.position[1] * metric.normal[1]) +
                             (pad.position[2] * metric.normal[2]);
            Vec3 shifted{};
            if (doorScale < 1.0f) {
                distance = (distance - metric.min) * (1.0f - doorScale);
                for (size_t i = 0; i < 3; i++) {
                    shifted[i] = pad.position[i] - (metric.normal[i] * distance);
                }
            } else {
                distance = (distance - metric.min) * (doorScale - 1.0f);
                for (size_t i = 0; i < 3; i++) {
                    shifted[i] = pad.position[i] + (metric.normal[i] * distance);
                }
            }
            int walked = pad.stanTile;
            if (stan_->WalkTiles(&walked, pad.position[0], pad.position[2], shifted[0], shifted[2])) {
                pad.stanTile = walked;
                pad.position = shifted;
                box[0] *= doorScale;
                box[1] *= doorScale;
            }
        } else {
            box[0] *= doorScale;
            box[1] *= doorScale;
        }
    }

    // The model's local X/Y/Z fit the pad box's Y/Z/X extents respectively
    // (setupDoor's bb2 axis remap).
    const ModelBounds& bounds = *model->bounds;
    float xscale = (box[3] - box[2]) / (bounds.xmax - bounds.xmin);
    float yscale = (box[5] - box[4]) / (bounds.ymax - bounds.ymin);
    float zscale = (box[1] - box[0]) / (bounds.zmax - bounds.zmin);
    if (xscale <= kMinimumScaleRatio || yscale <= kMinimumScaleRatio || zscale <= kMinimumScaleRatio) {
        xscale = yscale = zscale = 1.0f;
    }

    Mtxf oriented = Multiply(RotationZ(kHalfPi), RotationX(kHalfPi));
    oriented = Multiply(BasisToward({ -pad.look[0], -pad.look[1], -pad.look[2] }, pad.up), oriented);
    ScaleRowBasis(&oriented, 0, xscale);
    ScaleRowBasis(&oriented, 1, yscale);
    ScaleRowBasis(&oriented, 2, zscale);
    ScaleRows(&oriented, model->scale);

    BoundPad scaledBound;
    scaledBound.pad = pad;
    scaledBound.box = box;
    const Vec3 centre = BoundPadCentre(scaledBound);

    const auto doorType = static_cast<DoorMotionType>(def.doorType);
    Vec3 displacement{};
    if (doorType == DoorMotionType::Vertical || doorType == DoorMotionType::Fallaway) {
        const float extent = box[5] - box[4];
        displacement = { pad.look[0] * extent, pad.look[1] * extent, pad.look[2] * extent };
    } else {
        const float extent = box[2] - box[3];
        displacement = { pad.up[0] * extent, pad.up[1] * extent, pad.up[2] * extent };
    }

    // getposstan with no test radius: the pad's own position and tile stand in
    // unmodified once it's known to have one.
    const Vec3 doorPos = pad.position;
    const int doorStan = pad.stanTile;
    Vec3 propPos = centre;
    int propStan = doorStan;
    if ((def.flags2 & ObjectFlag2::PlaceAtPad) == 0) {
        if (!stan_->WalkTiles(&propStan, doorPos[0], doorPos[2], centre[0], centre[2])) {
            propPos = doorPos;
            propStan = doorStan;
        }
    } else {
        propPos = doorPos;
        propStan = doorStan;
    }

    if (props_ == nullptr) {
        return false;
    }
    const int propIndex = props_->Allocate();
    if (propIndex < 0) {
        return false;
    }

    PlacedDoor door;
    door.def = defIndex;
    door.model = def.model;
    door.flags = def.flags | ObjectFlag::ForceCollisions;
    door.flags2 = def.flags2;
    door.prop = propIndex;
    door.baseMatrix = oriented;
    door.centre = centre;
    door.displacementAxis = displacement;
    door.modelBounds = bounds;
    door.bbox = bounds;
    door.doorType = doorType;
    door.doorFlags = def.doorFlags;
    door.maxFrac = def.doorMaxFrac;
    door.perimFrac = def.doorPerimFrac;
    door.accel = def.doorAccel;
    door.decel = def.doorDecel;
    door.maxSpeed = def.doorMaxSpeed;
    door.keyFlags = def.doorKeyFlags;
    door.autoCloseFrames = def.doorAutoCloseFrames;
    door.openPosition = (door.flags & ObjectFlag::OpenByDefault) != 0 ? door.maxFrac : 0.0f;
    door.speed = 0.0f;
    door.state = DoorState::Stationary;
    door.bound = scaledBound;
    door.collision = CollisionBlock{};

    Prop& record = (*props_)[propIndex];
    record.type = PropType::Door;
    record.position = propPos;
    record.stan = propStan;

    UpdateBbox(&door);
    UpdateNextColour(&door);
    door.shadeColour = door.nextColour;

    if ((def.flags & ObjectFlag::CullBehindDoor) != 0) {
        door.portal = portalNumber;
        if (portalNumber >= 0 && door.openPosition == 0.0f) {
            SetPortalDisabled(door, true);
        }
    }

    record.rooms[0] = static_cast<uint8_t>(stan_->TileRoom(propStan));
    props_->RegisterRoom(propIndex, record.rooms[0]);
    record.rooms[1] = kNoRoom;
    record.rooms[2] = kNoRoom;
    if ((def.flags & (ObjectFlag::CullBehindDoor | ObjectFlag::NoPortalClose)) != 0) {
        int secondRoom = -1;
        if (room1 != record.rooms[0]) {
            if (room1 >= 0) {
                secondRoom = room1;
            }
        } else if (room2 >= 0) {
            secondRoom = room2;
        }
        if (secondRoom >= 0) {
            record.rooms[1] = static_cast<uint8_t>(secondRoom);
            props_->RegisterRoom(propIndex, secondRoom);
        }
    }
    // TODO(https://github.com/liz-baker/frigate/issues/153): modelSetScale's
    // mutation of the shared model's own scale record by
    // max(xscale,yscale,zscale) -- read back by weapon/projectile hit-testing
    // against the door's model, which isn't ported yet.

    doors_.push_back(door);
    const size_t index = doors_.size() - 1;
    record.object = static_cast<int>(index);
    if (byDef_.size() <= defIndex) {
        byDef_.resize(defIndex + 1, -1);
    }
    byDef_[defIndex] = static_cast<int>(index);
    props_->Activate(propIndex);
    props_->Enable(propIndex);
    return true;
}

void Doors::LinkDoors(const StageSetup& setup) {
    const auto& defs = setup.PropDefs();
    for (PlacedDoor& door : doors_) {
        const int32_t offset = defs[door.def].doorLinkedOffset;
        if (offset == 0) {
            continue;
        }
        const int64_t targetDef = static_cast<int64_t>(door.def) + offset;
        if (targetDef < 0 || static_cast<size_t>(targetDef) >= byDef_.size() ||
            byDef_[static_cast<size_t>(targetDef)] < 0) {
            continue;
        }
        door.linkedDoor = byDef_[static_cast<size_t>(targetDef)];
    }
}

void Doors::SetPortalDisabled(const PlacedDoor& door, bool disabled) {
    if (door.portal >= 0 && level_ != nullptr) {
        level_->SetPortalDisabled(static_cast<size_t>(door.portal), disabled);
    }
}

void Doors::StartOpen(PlacedDoor* door) {
    door->flags &= ~ObjectFlag::DoorKeepOpen;
    door->runtimeFlags |= ObjectRuntime::BeenOpened;
    // doorPlayOpenSound0: no audio is ported yet (see Stage.cpp's #41 TODOs).
    SetPortalDisabled(*door, false);
    if (door->doorType == DoorMotionType::Fallaway) {
        door->flags &= ~ObjectFlag::ForceCollisions;
        door->flags |= ObjectFlag::CannotActivate;
        door->perimFrac = 0.0f;
        if (door->collision) {
            door->collision->edges = 0;
        }
    }
}

void Doors::StartClose(PlacedDoor* door) {
    door->flags &= ~ObjectFlag::DoorKeepOpen;
    // doorPlayOpenSound1: no audio is ported yet.
}

void Doors::FinishOpen(PlacedDoor* door) {
    // doorPlayCloseSound0: no audio is ported yet.
    // TODO(https://github.com/liz-baker/frigate/issues/180): a
    // DoorMotionType::Fallaway door's hand-off to the projectile/embedment
    // system once it finishes falling away -- neither system exists yet.
    (void)door;
}

void Doors::FinishClose(PlacedDoor* door) {
    // doorPlayCloseSound1: no audio is ported yet.
    SetPortalDisabled(*door, true);
}

void Doors::OpenInstant(int doorIndex, int32_t globalTimer) {
    PlacedDoor& door = doors_[static_cast<size_t>(doorIndex)];
    door.speed = 0.0f;
    door.openPosition = door.maxFrac;
    door.openedTime = static_cast<uint32_t>(globalTimer);
    door.state = DoorState::Stationary;
    UpdateBbox(&door);
    // doorActivatePortal, then door7F053B10, which only stops the door's sounds.
    SetPortalDisabled(door, false);
}

void Doors::SetOpenState(PlacedDoor* door, DoorState newState) {
    if (newState == DoorState::Opening) {
        if (door->state == DoorState::Stationary || door->state == DoorState::Waiting) {
            StartOpen(door);
        }
        door->state = newState;
    } else if (newState == DoorState::Closing) {
        if (door->state == DoorState::Stationary && door->openPosition > 0.0f) {
            StartClose(door);
        }
        if ((door->state != DoorState::Stationary && door->state != DoorState::Waiting) || door->openPosition > 0.0f) {
            door->state = newState;
        } else if (door->state == DoorState::Waiting) {
            door->state = DoorState::Stationary;
        }
    } else {
        door->state = newState;
    }
}

void Doors::Activate(int doorIndex, DoorState state) {
    if (doorIndex < 0 || static_cast<size_t>(doorIndex) >= doors_.size()) {
        return;
    }
    PlacedDoor& door = doors_[static_cast<size_t>(doorIndex)];
    DoorState linkedState = state;
    if ((door.flags2 & ObjectFlag2::DoorCloseFirst) != 0 && state == DoorState::Opening) {
        linkedState = DoorState::Closing;
        if (door.state == DoorState::Stationary) {
            state = DoorState::Waiting;
        }
    }
    SetOpenState(&door, state);
    int sibling = door.linkedDoor;
    while (sibling >= 0 && sibling != doorIndex) {
        SetOpenState(&doors_[static_cast<size_t>(sibling)], linkedState);
        sibling = doors_[static_cast<size_t>(sibling)].linkedDoor;
    }
}

void Doors::UpdateBbox(PlacedDoor* door) {
    door->bbox = door->modelBounds;
    if ((door->doorFlags & DoorFlag::ClipToBbox) != 0) {
        if (door->doorType == DoorMotionType::Vertical) {
            door->bbox.ymax =
                door->modelBounds.ymax + ((door->modelBounds.ymin - door->modelBounds.ymax) * door->openPosition);
        } else {
            door->bbox.xmin =
                door->modelBounds.xmin + ((door->modelBounds.xmax - door->modelBounds.xmin) * door->openPosition);
        }
    }
    if (!door->collision) {
        return;
    }
    if (door->perimFrac <= door->openPosition) {
        door->collision->edges = 0;
        return;
    }

    const Mtxf transform = Transform(*door);
    BuildCollisionPolygon(door->bbox, transform, &*door->collision);

    if (door->doorType == DoorMotionType::Vertical) {
        door->collision->bottom = door->centre[1] + SumMatrixPos(door->bbox, transform, 1);
    } else if (door->doorType == DoorMotionType::Fallaway) {
        door->collision->bottom = door->centre[1] - 10000.0f;
    } else {
        door->collision->bottom = transform.m[3][1] + SumMatrixPos(door->bbox, transform, 1);
        if ((door->doorFlags & DoorFlag::ExtendedY) != 0) {
            door->collision->bottom -= 1000.0f;
        }
    }

    const bool nearlyOpenEyeOrIris =
        (door->doorType == DoorMotionType::Eye || door->doorType == DoorMotionType::Iris) &&
        (door->openPosition - (0.4f * door->maxFrac) > 0.0f);
    if (nearlyOpenEyeOrIris) {
        door->collision->top = door->collision->bottom + 50.0f;
    } else if (door->doorType == DoorMotionType::Fallaway) {
        door->collision->top = door->centre[1] + 1000.0f;
    } else {
        door->collision->top = transform.m[3][1] + SumMatrixNeg(door->bbox, transform, 1);
        if ((door->doorFlags & DoorFlag::ExtendedY) != 0) {
            door->collision->top += 1000.0f;
        }
    }
}

void Doors::Tick(int doorIndex, const Timing& timing) {
    if (doorIndex < 0 || static_cast<size_t>(doorIndex) >= doors_.size()) {
        return;
    }
    PlacedDoor& door = doors_[static_cast<size_t>(doorIndex)];
    // TODO(https://github.com/liz-baker/frigate/issues/41): the iris door's
    // slide sounds, which play as openPosition crosses 30% of maxFrac.
    if (door.openedTime > 0 &&
        static_cast<int32_t>(door.openedTime) < timing.globalTimer - static_cast<int32_t>(door.autoCloseFrames) &&
        door.state == DoorState::Stationary && (door.flags & ObjectFlag::DoorKeepOpen) == 0) {
        Activate(doorIndex, DoorState::Closing);
    }

    if (door.state == DoorState::Waiting) {
        bool siblingsShut = true;
        for (int sibling = door.linkedDoor; sibling >= 0 && sibling != doorIndex;
             sibling = doors_[static_cast<size_t>(sibling)].linkedDoor) {
            const PlacedDoor& other = doors_[static_cast<size_t>(sibling)];
            if (other.state != DoorState::Stationary || other.openPosition > 0.0f) {
                siblingsShut = false;
            }
        }
        if (siblingsShut) {
            SetOpenState(&door, DoorState::Opening);
        }
    }

    if (door.doorType == DoorMotionType::Fallaway && IsClosed(door) && IsPadlockFree(door)) {
        ActivateWrapper(doorIndex);
    }

    if (door.lastCalcTime < timing.globalTimer || timing.clockTimer == 0) {
        // door7F054FB4.
        std::vector<int> ring;
        int cur = doorIndex;
        do {
            ring.push_back(cur);
            cur = doors_[static_cast<size_t>(cur)].linkedDoor;
        } while (cur >= 0 && cur != doorIndex);

        bool anyMoving = false;
        for (const int index : ring) {
            PlacedDoor& member = doors_[static_cast<size_t>(index)];
            member.lastCalcPosition = member.openPosition;
            if (UpdateDisplacement(&member, timing.clockTimer)) {
                anyMoving = true;
            }
        }

        bool unblocked = true;
        if (anyMoving) {
            for (const int index : ring) {
                UpdateBbox(&doors_[static_cast<size_t>(index)]);
                unblocked = !clear_ || clear_(doors_[static_cast<size_t>(index)].prop);
                if (!unblocked) {
                    break;
                }
            }
        }

        for (const int index : ring) {
            PlacedDoor& member = doors_[static_cast<size_t>(index)];
            if (anyMoving) {
                if (unblocked) {
                    if (member.state == DoorState::Opening && member.maxFrac <= member.openPosition) {
                        member.state = DoorState::Stationary;
                        member.speed = 0.0f;
                        member.openedTime = static_cast<uint32_t>(timing.globalTimer);
                        FinishOpen(&member);
                    } else if (member.state == DoorState::Closing && member.openPosition <= 0.0f) {
                        member.state = DoorState::Stationary;
                        member.speed = 0.0f;
                        member.openedTime = 0;
                        FinishClose(&member);
                    }
                    UpdateNextColour(&member);
                } else {
                    member.speed = 0.0f;
                    member.openPosition = member.lastCalcPosition;
                    UpdateBbox(&member);
                }
            }
            member.lastCalcTime = timing.globalTimer;
        }
    }
}

void Doors::ActivateWrapper(int doorIndex) {
    PlacedDoor& door = doors_[static_cast<size_t>(doorIndex)];
    if (door.state == DoorState::Opening || door.state == DoorState::Waiting) {
        Activate(doorIndex, DoorState::Closing);
    } else if (door.state == DoorState::Closing) {
        Activate(doorIndex, DoorState::Opening);
    } else if (door.state == DoorState::Stationary) {
        Activate(doorIndex, door.openPosition > 0.5f ? DoorState::Closing : DoorState::Opening);
    }
    door.runtimeFlags |= ObjectRuntime::Activated;
    door.flags2 &= ~ObjectFlag2::DoorTriedLocked;
    if (activated_) {
        activated_(door.prop);
    }
}

bool Doors::IsPadlockFree(const PlacedDoor& door) const {
    if ((door.runtimeFlags & ObjectRuntime::PadlockedDoor) == 0 || !padlocked_) {
        return true;
    }
    return !padlocked_(door.def);
}

bool Doors::IsInFront(const PlacedDoor& door, const Vec3& position) const {
    const Pad& pad = door.bound.pad;
    const Vec3 normal = { (pad.up[1] * pad.look[2]) - (pad.look[1] * pad.up[2]),
                          (pad.up[2] * pad.look[0]) - (pad.look[2] * pad.up[0]),
                          (pad.up[0] * pad.look[1]) - (pad.look[0] * pad.up[1]) };
    const float side = ((position[0] - pad.position[0]) * normal[0] + (position[1] - pad.position[1]) * normal[1]) +
                       (position[2] - pad.position[2]) * normal[2];
    if ((door.doorFlags & DoorFlag::Flip) != 0) {
        return -side >= 0.0f;
    }
    return side >= 0.0f;
}

void Doors::ChooseSwingDirection(int doorIndex, const Vec3& position) {
    PlacedDoor& door = doors_[static_cast<size_t>(doorIndex)];
    if ((door.flags & ObjectFlag::DoorTwoWay) == 0 || door.state != DoorState::Stationary ||
        door.openPosition != 0.0f) {
        return;
    }
    const bool infront = IsInFront(door, position);
    uint32_t want = 0;
    if ((door.doorFlags & DoorFlag::Flip) == 0) {
        if (!infront) {
            want = ObjectFlag::DoorOpenToFront;
        }
    } else if (infront) {
        want = ObjectFlag::DoorOpenToFront;
    }
    if (((door.flags ^ want) & ObjectFlag::DoorOpenToFront) != 0) {
        int sibling = doorIndex;
        do {
            doors_[static_cast<size_t>(sibling)].flags ^= ObjectFlag::DoorOpenToFront;
            sibling = doors_[static_cast<size_t>(sibling)].linkedDoor;
        } while (sibling >= 0 && sibling != doorIndex);
    }
}

Vec3 Doors::HingePoint(const PlacedDoor& door) const {
    const Pad& pad = door.bound.pad;
    const auto& box = door.bound.box;
    const Vec3 side = { (pad.up[1] * pad.look[2]) - (pad.up[2] * pad.look[1]),
                        (pad.up[2] * pad.look[0]) - (pad.up[0] * pad.look[2]),
                        (pad.up[0] * pad.look[1]) - (pad.up[1] * pad.look[0]) };
    const bool openToFront = (door.flags & ObjectFlag::DoorOpenToFront) != 0;
    const float extent = (door.doorType == DoorMotionType::AztecChair || openToFront) ? box[1] : box[0];
    Vec3 hinge{};
    for (size_t i = 0; i < 3; i++) {
        hinge[i] = pad.position[i] + (pad.up[i] * box[2]) + (side[i] * extent);
    }
    return hinge;
}

Mtxf Doors::Transform(const PlacedDoor& door) const {
    if (door.doorType == DoorMotionType::Swinging || door.doorType == DoorMotionType::AztecChair) {
        const Vec3 hingePoint = HingePoint(door);
        Vec3 toHinge{};
        for (size_t i = 0; i < 3; i++) {
            toHinge[i] = door.centre[i] - hingePoint[i];
        }
        Mtxf result = Multiply(IdentityWithPosition(toHinge), door.baseMatrix);
        const bool openToFront = (door.flags & ObjectFlag::DoorOpenToFront) != 0;
        const float angle =
            openToFront ? (kTau - ((door.openPosition * kTau) / 360.0f)) : ((door.openPosition * kTau) / 360.0f);
        const Mtxf rotation = (door.doorType == DoorMotionType::AztecChair) ? RotationZ(angle) : RotationY(angle);
        result = Multiply(rotation, result);
        result = Multiply(IdentityWithPosition(hingePoint), result);
        if ((door.doorFlags & DoorFlag::Flip) != 0) {
            ScaleRowBasis(&result, 2, -1.0f);
        }
        return result;
    }
    if (door.doorType == DoorMotionType::Eye || door.doorType == DoorMotionType::Iris) {
        Mtxf result = door.baseMatrix;
        for (size_t i = 0; i < 3; i++) {
            result.m[3][i] = door.centre[i];
        }
        if ((door.doorFlags & DoorFlag::Flip) != 0) {
            ScaleRowBasis(&result, 2, -1.0f);
        }
        return result;
    }
    Mtxf result = door.baseMatrix;
    for (size_t i = 0; i < 3; i++) {
        result.m[3][i] = (door.displacementAxis[i] * door.openPosition) + door.centre[i];
    }
    if ((door.doorFlags & DoorFlag::Flip) != 0) {
        ScaleRowBasis(&result, 2, -1.0f);
    }
    return result;
}

int Doors::CdTypesFor(const PlacedDoor& door) const {
    int types = 0;
    if (door.openPosition <= 0.0f) {
        types = CdType::ClosedDoors;
    } else {
        types = (door.maxFrac <= door.openPosition) ? CdType::OpenDoors : CdType::AjarDoors;
    }
    if ((door.flags2 & ObjectFlag2::AiCannotOperate) != 0) {
        types |= CdType::DoorsLockedToAi;
    }
    return types;
}

const PlacedDoor* Doors::DoorForProp(int prop) const {
    if (props_ == nullptr) {
        return nullptr;
    }
    const Prop& record = (*props_)[prop];
    if (record.type != PropType::Door || record.object < 0) {
        return nullptr;
    }
    return &doors_[static_cast<size_t>(record.object)];
}

PlacedDoor* Doors::ForProp(int prop) {
    return const_cast<PlacedDoor*>(DoorForProp(prop));
}

void Doors::UpdateNextColour(PlacedDoor* door) {
    if ((door->flags & ObjectFlag::Illuminated) != 0) {
        return;
    }
    const int tile = (*props_)[door->prop].stan;
    if (tile != kNoTile) {
        door->nextColour = ObjectShadeFromTile(stan_->Tile(tile));
    }
}

bool Doors::Passes(int prop, int cdTypes) const {
    const PlacedDoor* door = DoorForProp(prop);
    if (door == nullptr) {
        return false;
    }
    if ((cdTypes & CdType::AiOpaque) != 0 && (door->flags & ObjectFlag::AiSeesThrough) != 0) {
        return false;
    }
    if ((cdTypes & CdType::Doors) == 0 && (CdTypesFor(*door) & cdTypes) == 0) {
        return false;
    }
    return true;
}

const CollisionBlock* Doors::Collision(int prop) const {
    const PlacedDoor* door = DoorForProp(prop);
    if (door == nullptr || !door->collision) {
        return nullptr;
    }
    return &*door->collision;
}

} // namespace Frigate::Level
