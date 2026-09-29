#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "level/BgHeader.h"
#include "level/IntroSection.h"
#include "level/PlayerWeapons.h"
#include "level/Stan.h"

namespace Frigate::Level {

class PropList;

// N64 controller button masks (PR/os.h).
namespace Buttons {
constexpr uint16_t A = 0x8000;
constexpr uint16_t B = 0x4000;
constexpr uint16_t Z = 0x2000;
constexpr uint16_t Start = 0x1000;
constexpr uint16_t UpPad = 0x0800;
constexpr uint16_t DownPad = 0x0400;
constexpr uint16_t LeftPad = 0x0200;
constexpr uint16_t RightPad = 0x0100;
constexpr uint16_t L = 0x0020;
constexpr uint16_t R = 0x0010;
constexpr uint16_t UpC = 0x0008;
constexpr uint16_t DownC = 0x0004;
constexpr uint16_t LeftC = 0x0002;
constexpr uint16_t RightC = 0x0001;
} // namespace Buttons

// CONTROLLER_CONFIG_*: the control styles 1.1 to 1.4 (Honey, Solitare, Kissy,
// Goodnight) use one controller, and 2.1 to 2.4 (Plenty, Galore, Domino,
// Goodhead) two.
enum class ControlConfig : int32_t {
    Honey,
    Solitare,
    Kissy,
    Goodnight,
    Plenty,
    Galore,
    Domino,
    Goodhead,
    Cinema,
};

// CROUCH_*.
enum Crouch : int32_t {
    kCrouchSquat,
    kCrouchHalf,
    kCrouchStand,
};

// The controller state a tick reads. The second controller is only read by the
// two-controller styles.
struct MoveInput {
    int8_t stickX = 0;
    int8_t stickY = 0;
    uint16_t buttons = 0;
    int8_t stick2X = 0;
    int8_t stick2Y = 0;
    uint16_t buttons2 = 0;
};

// What the player's options say (the game's per-player option table).
struct PlayerOptions {
    ControlConfig controlType = ControlConfig::Honey;
    // cur_player_get_aim_control: aim mode toggles rather than being held.
    bool aimToggle = false;
    // get_cur_player_look_vertical_inverted.
    bool lookInverted = false;
    // cur_player_get_lookahead.
    bool lookAhead = true;
    // The screen setting is widescreen or cinema, or the ratio is 16:9.
    bool wideScreen = false;
};

// What a tick needs from the rest of the game.
struct MoveContext {
    // g_ClockTimer and g_GlobalTimerDelta.
    int clockTimer = 1;
    float globalTimerDelta = 1.0f;
    // viGetFovY().
    float fovY = 60.0f;
    // player_perspective_height.
    float perspectiveHeight = 1.0f;
    // lvlGetControlsLockedFlag().
    bool controlsLocked = false;
    PlayerOptions options;
    // bondwalkItemCheckBitflags(right hand's weapon, WEAPONSTATBITFLAG_DISABLE_CROUCH).
    bool weaponDisablesCrouch = false;
    // randomGetNext.
    std::function<uint32_t()> random;
    // The weapons the player holds, which the aim and the gun sway are set on;
    // null leaves them alone. `gunView` is the screen the aim is worked out on.
    PlayerWeapons* weapons = nullptr;
    GunView gunView;
};

// MoveData: what bondviewProcessInput makes of the controller. The weapon and
// aim fields are for the gun code.
struct MoveData {
    bool canSwivelGun = false;
    bool canManualAim = false;
    bool triggerOn = false;
    bool btap = false;
    bool canLookAhead = false;
    bool canTurnTank = false;
    bool canNaturalTurn = false;
    bool canNaturalPitch = false;
    bool digitalStepForward = false;
    bool digitalStepBack = false;
    bool digitalStepLeft = false;
    bool digitalStepRight = false;
    float tankTurnLeftSpeed = 0;
    float tankTurnRightSpeed = 0;
    float speedVertaDown = 0;
    float speedVertaUp = 0;
    float aimTurnLeftSpeed = 0;
    float aimTurnRightSpeed = 0;
    bool weaponBackOffset = false;
    bool weaponForwardOffset = false;
    bool aiming = false;
    bool zooming = false;
    float zoomOutFovPersec = 0;
    float zoomInFovPersec = 0;
    bool crouchDown = false;
    bool crouchUp = false;
    bool rLeanLeft = false;
    bool rLeanRight = false;
    bool detonating = false;
    bool canAutoAim = false;
    bool invertPitch = false;
    bool disableLookAhead = false;
    int32_t controlStickXSafe = 0;
    int32_t controlStickYSafe = 0;
    int32_t controlStickXRaw = 0;
    int32_t controlStickYRaw = 0;
    int32_t analogTurn = 0;
    int32_t analogPitch = 0;
    int32_t analogStrafe = 0;
    int32_t analogWalk = 0;
};

// Bond's head, as bondviewMoveAnimationTick drives it (bheadAdjustAnimation and
// bheadUpdate): the walking animation whose root motion is what moves the
// player forward and sideways. The player takes its position offset from it
// each tick.
// Without one the head holds the standing pose, so the player turns,
// crouches and is held by walls, but stick speed alone does not move it.
class HeadMotion {
  public:
    virtual ~HeadMotion() = default;
    // The head's position this tick, relative to the player's feet: x and z
    // are the horizontal displacement this tick (per unit of tick time), y is
    // the head's height. `speed` is the largest of the forward, sideways and
    // turning speeds, `speedForwards` and `speedSideways` the last two, and
    // `breathing` the player's breathing (bondbreathing).
    virtual Vec3 Tick(float speed, float speedForwards, float speedSideways, float breathing,
                      const MoveContext& context) = 0;
    // bheadGetBreathingValue: how fast the head breathes, given the player's
    // breathing. 0 without a walking animation.
    virtual float BreathingValue(float breathing) const {
        (void)breathing;
        return 0.0f;
    }
    // headanim == 1: the head is on the faster of the two walking animations.
    virtual bool OnFastAnimation() const {
        return false;
    }
    // headlook and headup after that tick: the view's direction and up vector
    // in the player's frame. The standing pose looks straight ahead.
    virtual Vec3 Look() const {
        return { 0.0f, 0.0f, 1.0f };
    }
    virtual Vec3 Up() const {
        return { 0.0f, 1.0f, 0.0f };
    }
};

// The part of `struct player` that walking reads and writes, and the per-tick
// movement and collision the game runs on it (MoveBond and what it calls, on
// foot).
class PlayerMove {
  public:
    // g_CurrentPlayer's state at the start of a stage: init_player_BONDdata
    // and the intro's placement of the player.
    void Init(const PlayerSpawn& spawn, const Stan& stan);

    // bondviewMovePlayerUpdateViewport's call to MoveBond: reads the
    // controller and moves the player through `stan`.
    void Tick(const MoveContext& context, const MoveInput& input, Stan& stan);

    // The head motion each tick reads; null for the standing pose. Not owned.
    void SetHead(HeadMotion* head) {
        head_ = head;
    }
    // The player's prop, in `props`: walking keeps its position and tile, and
    // its room in the room lists, current. Not owned.
    void SetProp(PropList* props, int prop) {
        props_ = props;
        prop_ = prop;
    }
    // bondviewGetPropHeightRelatedValues: the player's footprint, filling
    // `out`. False while the player's prop is hidden from collision tests.
    bool Footprint(PropCollider* out) const;

    // vv_theta and vv_verta, in degrees.
    float Theta() const {
        return theta_;
    }
    float Verta() const {
        return verta_;
    }
    // field_488.theta_transform: the horizontal look direction.
    const Vec3& LookDirection() const {
        return thetaTransform_;
    }
    // field_488.applied_view and applied_view2: the camera's look direction
    // and up vector, the head's tilt applied to the heading and pitch.
    const Vec3& AppliedView() const {
        return appliedView_;
    }
    const Vec3& AppliedViewUp() const {
        return appliedView2_;
    }
    // field_488.collision_position: the player's position on the floor plane,
    // with the eye height above it in y.
    const Vec3& CollisionPosition() const {
        return collisionPosition_;
    }
    // field_488.pos: where the camera is.
    const Vec3& Position() const {
        return position_;
    }
    // bondprevpos.
    const Vec3& PreviousPosition() const {
        return previousPosition_;
    }
    // field_488.current_tile_ptr and its room (registeredroom).
    int Tile() const {
        return tile_;
    }
    int Room() const {
        return room_;
    }
    // stanHeight: the floor height under the player.
    float StanHeight() const {
        return stanHeight_;
    }
    // field_70: the floor height the eye follows, which lags StanHeight()
    // while climbing and falls under gravity above it.
    float StanHeightRemainder() const {
        return stanHeightRemainder_;
    }
    // eyeheight.
    float EyeHeight() const {
        return eyeHeight_;
    }
    float SpeedForwards() const {
        return speedForwards_;
    }
    float SpeedSideways() const {
        return speedSideways_;
    }
    float SpeedTheta() const {
        return speedTheta_;
    }
    // The crouch the player is in: the lower of the chosen and the forced one.
    int CrouchPosition() const {
        return crouchPosition_ < autoCrouchPosition_ ? crouchPosition_ : autoCrouchPosition_;
    }
    // ducking_height_offset.
    float DuckingHeightOffset() const {
        return duckingHeightOffset_;
    }
    // The controller reading of the last tick.
    const MoveData& LastMove() const {
        return move_;
    }
    // field_D0: B was pressed this tick and there was nothing to enter.
    bool UsePressed() const {
        return usePressed_;
    }
    // field_488.current_tile_ptr_for_portals and pos3: the tile under the camera
    // and the point on its floor the camera is over.
    int PortalTile() const {
        return portalTile_;
    }
    const Vec3& PortalPosition() const {
        return portalPosition_;
    }
    // collision_bounds.
    const std::array<float, 8>& CollisionBounds() const {
        return collisionBounds_;
    }
    bool InSightAim() const {
        return inSightAim_;
    }
    // field_AC.
    bool PropCollides() const {
        return propCollides_;
    }

  private:
    // bondviewProcessInput.
    void ProcessInput(const MoveContext& context, const MoveInput& input, Stan& stan);
    // bondviewApplyVertaTheta.
    void ApplyVertaTheta();
    // bondviewGetCollisionRadius.
    void CollisionRadius(float* radius, float* height, float* always30) const;
    // bondviewGetPlayerDuckingHeightRelated.
    float DuckingHeightRelated() const {
        return eyeHeight_ + bounceOffset_ + duckingHeightOffset_;
    }
    // bondviewYPositionRelated: the floor height on `tile`, or on the ladder
    // tile while the player is on one.
    float FloorHeight(const Stan& stan, int tile, float x, float z) const;
    // bondviewTryMoveToStan.
    bool TryMoveToStan(Stan& stan, const Vec3& position, int* tile);
    // bondviewTrySimpleMovePlayerCollision and its fallbacks.
    bool TrySimpleMove(Stan& stan, const Vec3& next, Vec3* pointA, Vec3* pointB);
    int TryFractionMove(Stan& stan, const Vec3& next, const Vec3& firstA, const Vec3& firstB, Vec3* pointA,
                        Vec3* pointB);
    int TryEdgeMove(Stan& stan, const Vec3& next, const Vec3& pointA, const Vec3& pointB);
    bool TryEndHopMove(Stan& stan, const Vec3& next, const Vec3& pointA, const Vec3& pointB);
    // bondviewCalcUpdatePlayerCollision.
    void CalcUpdateCollision(Stan& stan, const Vec3& offset, bool allowScoot);
    // The tail of bondviewMoveAnimationTick: builds appliedView_ and
    // appliedView2_ from the head's `look` and `up`.
    void UpdateAppliedView(const Vec3& look, const Vec3& up);
    // bondviewUpdatePlayerY.
    void UpdatePlayerY(Stan& stan, bool useStanHeight, float stanHeightOffset);
    // bondviewUpdatePlayerCollisionPositionFields.
    void UpdateCollisionPositionFields(Stan& stan);
    // chrobjApplySpeed.
    void ApplySpeed(float* position, float target, float* speed, float accel, float decel, float maxSpeed);
    // bondviewCurrentPlayerUpdateSpeedVerta and ...Theta.
    void UpdateSpeedVerta(float value);
    void UpdateSpeedTheta(float value);
    void UpdateSpeedSideways(int direction);
    void UpdateSpeedForwards(int direction);
    // currentPlayerAdjustCrouchPos.
    void AdjustCrouch(int delta);

    // The current tick's inputs from the rest of the game.
    MoveContext context_;
    MoveData move_;
    bool usePressed_ = false;
    // buttons_pressed and prev_buttons_pressed: the first and second
    // controller's buttons of the last tick.
    uint16_t buttonsPressed_ = 0;
    uint16_t prevButtons2_ = 0;
    // watch_animation_state's on-foot-freezing effect: true from the Start
    // press that opens the watch to the one that closes it.
    bool watchOpen_ = false;

    // Look angles.
    float theta_ = 0;
    float verta_ = -4.0f;
    float verta360_ = 356.0f;
    float cosTheta_ = 1.0f;
    float sinTheta_ = 0.0f;
    float cosVerta_ = 1.0f;
    float sinVerta_ = 0.0f;
    Vec3 thetaTransform_ = { 0.0f, 0.0f, 1.0f };
    Vec3 appliedView_ = { 0.0f, 0.0f, 1.0f };
    Vec3 appliedView2_ = { 0.0f, 1.0f, 0.0f };
    ControlConfig controlDef_ = ControlConfig::Honey;
    bool inSightAim_ = false;

    // Speeds.
    float speedTheta_ = 0;
    float speedVerta_ = 0;
    float speedSideways_ = 0;
    float speedStrafe_ = 0;
    float speedForwards_ = 0;
    float speedGo_ = 0;
    float speedBoost_ = 1.0f;
    int speedMaxTime60_ = 0;
    Vec3 shotSpeed_{};
    float breathing_ = 0;
    // swaytarget and the offsets it moves the player by.
    float swayTarget_ = 0;
    float swayOffset0_ = 0;
    float swayOffset2_ = 0;

    // Look ahead.
    bool lookAheadCentreEnabled_ = true;
    bool autoMoveCentreEnabled_ = true;
    bool fastMoveCentreEnabled_ = false;
    bool autoMoveCentre_ = true;
    bool moveCentreRelease_ = false;
    bool doCentreUpDown_ = false;
    bool prevUpDown_ = false;

    // Crouching.
    int crouchPosition_ = kCrouchStand;
    int autoCrouchPosition_ = kCrouchStand;
    float duckingHeightOffset_ = 0;
    // field_A4: the speed the ducking offset changes at.
    float duckingSpeed_ = 0;

    // Position and floor.
    Vec3 collisionPosition_{};
    Vec3 position_{};
    Vec3 portalPosition_{};
    Vec3 previousPosition_{};
    int tile_ = kNoTile;
    int portalTile_ = kNoTile;
    int room_ = 0;
    // The player's prop, and whether collision tests see it (field_AC).
    PropList* props_ = nullptr;
    int prop_ = -1;
    bool propCollides_ = true;
    float collisionRadius_ = 30.0f;
    float stanHeight_ = 0;
    // field_6C, field_70, field_7C: the smoothed floor height and its vertical
    // speed while falling.
    float stanHeightFilter_ = 0;
    float stanHeightRemainder_ = 0;
    float verticalSpeed_ = 0;
    // field_84 and field_88, field_8C and vertical_bounce_adjust: the view's
    // bounce after landing.
    float bounce_ = 0;
    float bounceOffset_ = 0;
    int bounceTicks_ = 0;
    float bounceAdjust_ = 0;
    // field_2A6C and field_2A70: standing on a ladder tile, and the tile the
    // player was on before.
    bool onLadder_ = false;
    int ladderTile_ = kNoTile;
    // field_29C0: how far the view drops when looking down.
    float lookDownDrop_ = 15.0f;
    // field_3B8 and field_3C4..: the smoothed camera position.
    Vec3 smoothedPosition_{};
    Vec3 smoothedScaled_{};
    // collision_bounds: the player's collision square, as four (x, z) points
    // each side of the position.
    std::array<float, 8> collisionBounds_{};

    // The head: its position this tick, and the standing pose's height, which
    // sets the eye height so the first tick agrees with
    // bondviewPlayerBeginLife's.
    HeadMotion* head_ = nullptr;
    Vec3 headPosition_{};
    float standHeight_ = 168.0f;
    float eyeHeight_ = 175.0f;
};

} // namespace Frigate::Level
