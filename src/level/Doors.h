#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
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
// clears into (doorBuildClippedVertices, see ClippedDoorVertices).
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
    // model->scale: the model's own scale, grown by the largest scale the pad fit
    // gave an axis.
    float scale = 1.0f;
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

    // The bound pad the door is fit to, with DoorScale applied: what the swing
    // hinge and the player's use test read.
    BoundPad bound;
    // runtime_bitflags (ObjectRuntime::*).
    uint32_t runtimeFlags = 0;
    // The marks shots left on the door (ObjectState::Marked and
    // MarkedBlended).
    uint8_t marks = 0;
    // unkbd: the shots that hit the door's glass; the third breaks it.
    int glassHits = 0;
    // The switch slots of the door's model that are hidden (a broken window).
    std::vector<uint8_t> hiddenSwitches;
    // A windowed door's tint distances, and the opacity of its glass worked
    // out this tick (calculatedopacity).
    int32_t tintDistance = 0;
    int32_t cullDistance = 0;
    int calculatedOpacity = 0xFF;

    float openPosition = 0;
    float speed = 0;
    DoorState state = DoorState::Stationary;
    // g_GlobalTimer at the moment the door finished opening, so autoCloseFrames
    // can later trigger DoorState::Closing on its own; 0 while shut.
    uint32_t openedTime = 0;
    // lastcalc60i and lastcalc60f: the clock the door last advanced at, and the
    // position it had before, which a blocked door returns to.
    int32_t lastCalcTime = 0;
    float lastCalcPosition = 0;

    // The portal this door's pad straddles, -1 for none.
    int portal = -1;
    // The next door in this door's sibling ring (DoorRecord::linkedDoor), -1
    // for none; the ring is circular, ending back at the door it started from.
    int linkedDoor = -1;

    std::optional<CollisionBlock> collision;

    // The slot in the projectile pool of the record a door that fell away flies
    // with, -1 for none (ObjectRuntime::HasProjectile).
    int projectile = -1;

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
    // doorFinishOpen's hand-off of a door that falls away to the projectile code.
    void SetFallHook(std::function<void(PlacedDoor&)> hook) {
        fallHook_ = std::move(hook);
    }

    // setupDoor: creates the door propdef record `def` on its bound pad. False
    // when the record isn't a door or it could not be placed.
    bool Create(const StageSetup& setup, size_t def);
    // The tail of setupDoor's per-record loop, run once every door exists:
    // resolves each door's linkedDoorOffset into linkedDoor.
    void LinkDoors(const StageSetup& setup);

    // doorActivate: sets this door's state and, through its sibling ring,
    // every linked door's (DoorFlag2::DoorCloseFirst closes a sibling instead
    // of opening it in step).
    void Activate(int doorIndex, DoorState state);
    // AI_DoorOpenInstant: the door is fully open at once (its portal opened),
    // stationary, having opened at `globalTimer`.
    void OpenInstant(int doorIndex, int32_t globalTimer);
    // doorActivateWrapper: toggles the door between opening and closing, marks
    // it activated and runs the hook SetActivatedHook registered.
    void ActivateWrapper(int doorIndex);
    // The per-tick state objTick keeps a door in: the clock it runs against.
    struct Timing {
        int32_t clockTimer = 1;
        int32_t globalTimer = 0;
    };
    // objTick's door branch (the auto-close, the wait for a sibling, a fallaway
    // door's own trigger) and door7F054FB4: advances the door and its
    // siblings' openPosition (updateDoorDisplacement), rebuilds their
    // collision polygon (doorUpdateBbox) and toggles their portal on finishing
    // an open or close.
    void Tick(int doorIndex, const Timing& timing);

    // sub_GAME_7F03E6A0 (run at the end of doorActivateWrapper): what else the
    // activated door's prop triggers.
    void SetActivatedHook(std::function<void(int)> hook) {
        activated_ = std::move(hook);
    }
    // doorIsPadlockFree's search: whether a lock-door record for the door
    // made from propdef record `def` has its padlock intact.
    void SetPadlockCheck(std::function<bool(size_t)> locked) {
        padlocked_ = std::move(locked);
    }
    // sub_GAME_7F0448A8 for the door's prop: whether nothing stands where the
    // door has moved to.
    void SetClearCheck(std::function<bool(int)> clear) {
        clear_ = std::move(clear);
    }

    // doorIsClosed.
    static bool IsClosed(const PlacedDoor& door) {
        return (door.state == DoorState::Stationary || door.state == DoorState::Waiting) && door.openPosition <= 0.0f;
    }
    // posIsInFrontOfDoor: which side of the door's pad `position` is on.
    bool IsInFront(const PlacedDoor& door, const Vec3& position) const;
    // doorsChooseSwingDirection: a two-way door shut and still swings away
    // from `position`, for the door and its siblings.
    void ChooseSwingDirection(int doorIndex, const Vec3& position);
    // The hinge point DoorMotionType::Swinging/AztecChair rotate around
    // (door7F0526EC), which follows the side the door currently opens to.
    Vec3 HingePoint(const PlacedDoor& door) const;

    const std::vector<PlacedDoor>& All() const {
        return doors_;
    }
    const PlacedDoor* ForDef(size_t def) const;
    PlacedDoor* ForDef(size_t def) {
        return const_cast<PlacedDoor*>(std::as_const(*this).ForDef(def));
    }
    // The door the prop `prop` is, null when it is not one.
    PlacedDoor* ForProp(int prop);
    // PitemZ_entries[index]; null for an index outside the table or a stage
    // with no model table loaded.
    const PropModel* Model(int index) const {
        return models_ ? models_->Model(index) : nullptr;
    }

    // doorIsPadlockFree: false while a linked padlock object is still intact.
    // The decomp checks this in propdoorInteract and for a fallaway door's own
    // trigger, not inside doorActivate.
    bool IsPadlockFree(const PlacedDoor& door) const;
    // propDoorGetCdTypes folded into propIsOfCdType's door branch: whether the
    // door prop passes every bit `cdTypes` asks for.
    bool Passes(int prop, int cdTypes) const;
    // objTick's windowed door branch: the glass's opacity from the distance to
    // `viewer`, and the portal the door closes while the glass is whole and
    // opaque and the door is shut. `glassBroken` is whether the window is gone.
    void TickWindow(PlacedDoor* door, const Vec3& viewer, bool glassBroken);
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

    std::function<void(int)> activated_;
    std::function<void(PlacedDoor&)> fallHook_;
    std::function<bool(size_t)> padlocked_;
    std::function<bool(int)> clear_;
    std::vector<PlacedDoor> doors_;
    std::vector<int> byDef_;
    Stan* stan_ = nullptr;
    BgLevel* level_ = nullptr;
    std::shared_ptr<const PropModels> models_;
    PropList* props_ = nullptr;
};

} // namespace Frigate::Level
