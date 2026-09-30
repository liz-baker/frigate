#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "level/Matrix.h"
#include "level/Shading.h"
#include "level/Objects.h"
#include "level/PropList.h"
#include "level/StageSetup.h"
#include "level/StanProps.h"

namespace Frigate::Level {

class BgLevel;
class Stan;

// DOORSTATE: a door's current motion.
enum class DoorState : uint8_t {
    Stationary,
    Opening,
    Closing,
    // Waiting for a sibling door to finish closing (DoorFlag2::DoorCloseFirst).
    Waiting,
};

// DOORTYPE: how a door's live transform is built from its openPosition
// (door7F0526EC and doorUpdateBbox branch on this).
enum class DoorMotionType : uint16_t {
    Sliding,
    Flexi1,
    Flexi2,
    Flexi3,
    Vertical,
    Swinging,
    Eye,
    Iris,
    Fallaway,
    AztecChair,
};

// Bits of DoorRecord::doorFlags (a u16 distinct from ObjectRecord::flags).
namespace DoorFlag {
constexpr uint16_t ExtendedY = 0x1;
constexpr uint16_t Windowed = 0x2;
// Resizes the mesh as the door slides so it doesn't poke through the wall it
// clears into (doorBuildClippedVertices; not ported, see
// https://github.com/liz-baker/frigate/issues/178).
constexpr uint16_t ClipToBbox = 0x4;
constexpr uint16_t Flip = 0x8;
} // namespace DoorFlag

// The part of a DoorRecord setupDoor and doorInit build, plus the state
// machine fields doorActivate/updateDoorDisplacement advance.
struct PlacedDoor {
    size_t def = 0;
    int model = 0;
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    int prop = -1;

    // The door's orientation and pad-fit scale, before doorType's live
    // transform (Doors::Transform) is applied on top of it.
    Mtxf baseMatrix{};
    // runtime_pos: the door's swing hinge or slide origin.
    Vec3 centre{};
    // frac/unkac/unkb0: the direction and distance a fully-open sliding or
    // shutter door has moved from centre.
    Vec3 displacementAxis{};
    // The model's own bounding box, in model units.
    ModelBounds modelBounds{};
    // door->bbox: modelBounds, resized live for DoorFlag::ClipToBbox.
    ModelBounds bbox{};

    DoorMotionType doorType = DoorMotionType::Sliding;
    uint16_t doorFlags = 0;
    float maxFrac = 0;
    float perimFrac = 0;
    float accel = 0;
    float decel = 0;
    float maxSpeed = 0;
    uint32_t keyFlags = 0;
    uint32_t autoCloseFrames = 0;

    float openPosition = 0;
    float speed = 0;
    DoorState state = DoorState::Stationary;
    // g_GlobalTimer at the moment the door finished opening, so autoCloseFrames
    // can later trigger DoorState::Closing on its own. Not set by Tick yet --
    // there is no global timer to stamp it with, and nothing reads it, until
    // https://github.com/liz-baker/frigate/issues/166 wires a per-prop tick
    // dispatch through with the stage clock.
    uint32_t openedTime = 0;

    // The hinge point DoorMotionType::Swinging/AztecChair rotate around
    // (door7F0526EC), precomputed from the bound pad's box at creation.
    Vec3 hingePoint{};
    // The portal this door's pad straddles, -1 for none.
    int portal = -1;
    // The next door in this door's sibling ring (DoorRecord::linkedDoor), -1
    // for none; the ring is circular, ending back at the door it started from.
    int linkedDoor = -1;

    std::optional<CollisionBlock> collision;

    // shadecol and nextcol: the floor-tile shading the door is drawn with and
    // the shading it eases towards.
    ShadeColour shadeColour{};
    ShadeColour nextColour{};
};

// setupDoor, the door state machine (doorActivate/doorSetOpenState/
// updateDoorDisplacement) and propDoorGetCdTypes: the stage's doors, sharing
// the Objects' PropList (Reset's `props`) the way every other prop type does.
class Doors final {
  public:
    // init_load_objpos_table's share of door state: empties the door list.
    // `stan`, `level` and `models` are read the way Objects::Reset reads them;
    // `props` is the shared prop pool (Objects::Props()).
    void Reset(Stan* stan, BgLevel* level, std::shared_ptr<const PropModels> models, PropList* props);

    // setupDoor: creates the door propdef record `def` on its bound pad. False
    // when the record isn't a door or it could not be placed.
    bool Create(const StageSetup& setup, size_t def);
    // The tail of setupDoor's per-record loop, run once every door exists:
    // resolves each door's linkedDoorOffset into linkedDoor.
    void LinkDoors(const StageSetup& setup);

    // doorActivate: sets this door's state and, through its sibling ring,
    // every linked door's (DoorFlag2::DoorCloseFirst closes a sibling instead
    // of opening it in step). Not wired to a trigger yet -- proximity/guard
    // activation is https://github.com/liz-baker/frigate/issues/202.
    void Activate(int doorIndex, DoorState state);
    // door7F054FB4: advances the door and its siblings' openPosition
    // (updateDoorDisplacement), rebuilds their collision polygon
    // (doorUpdateBbox) and toggles their portal on finishing an open or
    // close. Not called from the stage tick loop yet -- chrpropTick's
    // per-prop dispatch is https://github.com/liz-baker/frigate/issues/166.
    void Tick(int doorIndex, int32_t clockTimer);

    const std::vector<PlacedDoor>& All() const {
        return doors_;
    }
    const PlacedDoor* ForDef(size_t def) const;
    // The door the prop `prop` is, null when it is not one.
    PlacedDoor* ForProp(int prop);
    // PitemZ_entries[index]; null for an index outside the table or a stage
    // with no model table loaded.
    const PropModel* Model(int index) const {
        return models_ ? models_->Model(index) : nullptr;
    }

    // doorIsPadlockFree: false while a linked padlock object is still intact.
    // Always true until padlock objects are placed
    // (https://github.com/liz-baker/frigate/issues/179). The decomp checks
    // this in propdoorInteract before calling doorActivate, not inside it; a
    // future caller (https://github.com/liz-baker/frigate/issues/202) should
    // do the same.
    bool IsPadlockFree(const PlacedDoor& door) const {
        (void)door;
        return true;
    }
    // propDoorGetCdTypes folded into propIsOfCdType's door branch: whether the
    // door prop passes every bit `cdTypes` asks for.
    bool Passes(int prop, int cdTypes) const;
    // chraiGetCollisionBounds for a door: its footprint, null when
    // perimFrac <= openPosition (the door has swung clear of it).
    const CollisionBlock* Collision(int prop) const;
    // door7F0526EC plus its displacement translation: the door's current
    // world transform, for drawing.
    Mtxf Transform(const PlacedDoor& door) const;

  private:
    void SetOpenState(PlacedDoor* door, DoorState state);
    // doorUpdateBbox.
    void UpdateBbox(PlacedDoor* door);
    // doorActivatePortal/doorDeactivatePortal.
    void SetPortalDisabled(const PlacedDoor& door, bool disabled);
    // doorStartOpen/doorStartClose/doorFinishOpen/doorFinishClose, minus the
    // sound cues (no audio is ported yet -- see the TODOs in Stage.cpp citing
    // https://github.com/liz-baker/frigate/issues/41).
    void StartOpen(PlacedDoor* door);
    void StartClose(PlacedDoor* door);
    void FinishOpen(PlacedDoor* door);
    void FinishClose(PlacedDoor* door);
    // sub_GAME_7F0402B4: the shading the door's tile gives it, unless it is
    // illuminated.
    void UpdateNextColour(PlacedDoor* door);
    // propDoorGetCdTypes.
    int CdTypesFor(const PlacedDoor& door) const;
    // The door record.object in the shared PropList names, or null when
    // `prop` is not a door.
    const PlacedDoor* DoorForProp(int prop) const;

    std::vector<PlacedDoor> doors_;
    std::vector<int> byDef_;
    Stan* stan_ = nullptr;
    BgLevel* level_ = nullptr;
    std::shared_ptr<const PropModels> models_;
    PropList* props_ = nullptr;
};

} // namespace Frigate::Level
