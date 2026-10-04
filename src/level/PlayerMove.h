#pragma once

#include <optional>
#include <string_view>

#include <array>
#include <cstdint>
#include <functional>

#include "audio/SoundPlayer.h"

#include "level/BgHeader.h"
#include "level/IntroSection.h"
#include "level/PlayerWeapons.h"
#include "level/Stan.h"
#include "level/Tank.h"

namespace Frigate::Level {

class PropList;
struct PlacedObject;

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

// The control style as the options screen names it: "1.1" to "1.4" and "2.1"
// to "2.4". Nothing for any other text.
std::optional<ControlConfig> ParseControlStyle(std::string_view text);

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
    // The first controller's stick at the last sample of the frame before
    // (joy7000C174 and joy7000C284), and the buttons that went down at any
    // sample of this frame (joyGetButtonsPressedThisFrame), which the watch
    // menu reads.
    int8_t prevStickX = 0;
    int8_t prevStickY = 0;
    uint16_t pressed = 0;
    // joyGetControllerCount: the controllers connected, or a demo's recorded count.
    int controllers = 1;
};

// What the player's options say (the game's per-player option table).
// SCREEN_SIZE_*: the screen setting of the options.
enum class ScreenSize : int32_t {
    Full,
    Wide,
    Cinema,
};

// The player's option settings (the game_options_entries and the control type).
struct PlayerOptions {
    ControlConfig controlType = ControlConfig::Honey;
    // cur_player_get_aim_control: aim mode toggles rather than being held.
    bool aimToggle = false;
    // get_cur_player_look_vertical_inverted.
    bool lookInverted = false;
    // cur_player_get_lookahead.
    bool lookAhead = true;
    // cur_player_get_autoaim.
    bool autoAim = true;
    // cur_player_get_sight_onscreen_control and cur_player_get_ammo_onscreen_setting.
    bool sightOnScreen = true;
    bool ammoOnScreen = true;
    // cur_player_get_screen_setting, and get_screen_ratio() == SCREEN_RATIO_16_9.
    ScreenSize screen = ScreenSize::Full;
    bool ratio16x9 = false;

    // The screen setting is widescreen or cinema, or the ratio is 16:9.
    bool WideScreen() const {
        return screen != ScreenSize::Full || ratio16x9;
    }
    // fileLoadSettingsForFolder: the options the option bits of a save file
    // set (docs/formats/goldeneye-ramrom.md).
    static PlayerOptions FromSaveBits(uint16_t bits);
};

// The parts of the watch that MoveBond runs: bondviewProcessInput opens it on
// a Start press and only reads the controls while it is closed, and the tick
// steps it.
class WatchHooks {
  public:
    virtual ~WatchHooks() = default;
    // watch_animation_state.
    virtual int32_t State() const = 0;
    // A Start press this frame (or none) opens or closes the watch, unless
    // `blocked`: the player is dead or being disarmed.
    virtual void OnInput(bool startPressed, bool blocked) = 0;
    // bondviewTriggerWatchZoom(target) and bondviewUpdateWatchZoomIn, which
    // run while the watch is closed.
    virtual void UpdateZoom(float target) = 0;
    // zoominfovy: the field of view the zoom has reached.
    virtual float ZoomFovY() const = 0;
    // bondviewWatchAnimationTick.
    virtual void Tick() = 0;
};

// What the tank's movement reaches the stage's objects and items through.
class TankHooks {
  public:
    virtual ~TankHooks() = default;
    // The object a prop is when it is a tank (PROP_TYPE_OBJ with obj->type
    // PROPDEF_TANK); null otherwise.
    virtual PlacedObject* TankOf(int prop) = 0;
    // The tank's model: its nodes, and chrobjGetBboxFromObjectRecord's hull.
    virtual const TankGeometry* Geometry(const PlacedObject& tank) = 0;
    virtual const ModelBounds* Hull(const PlacedObject& tank) = 0;
    // chraiGetCollisionBoundsWithoutY.
    virtual bool CollisionBounds(int prop, PropCollider* out) = 0;
    // sub_GAME_7F03D058: takes a prop out of the collision tests (`collides`
    // false) or puts it back.
    virtual void SetCollides(int prop, bool collides) = 0;
    // The player gets into the tank: bondinvAddInvItem(ITEM_TANKSHELLS), and the
    // shells the tank held become the player's ammo.
    virtual void EnterTank(int32_t ammo) = 0;
    // The player gets out: the shells left (the ammo held and what is in the
    // magazine of the right hand's gun) go back to the tank, and the shells leave
    // the inventory. Returns the shells the tank now holds.
    virtual int32_t LeaveTank() = 0;

    // What the tank's prop pass reads from the player.
    struct Frame {
        PlacedObject* tank = nullptr;
        int prop = -1;
        Vec3 collisionPosition{};
        int tile = kNoTile;
        float tankOrientation = 0.0f;
        float turretVerticalAngle = 0.0f;
        float turretOrientation = 0.0f;
        Vec3 modelOffset{};
        bool firing = false;
        int clockTimer = 1;
        // g_TankDamagePenaltyTicks, which crushing a prop sets.
        int damagePenaltyTicks = 0;
    };
    // The end of MoveBond while the player drives: the tank follows the player
    // and crushes what it drives into. Returns the damage penalty ticks.
    virtual int TickTank(const Frame& frame) = 0;
};

// What a tick needs from the rest of the game.
struct MoveContext {
    // g_ClockTimer and g_GlobalTimerDelta.
    int clockTimer = 1;
    float globalTimerDelta = 1.0f;
    // viGetFovY() as MoveBond starts. The watch's zoom update inside the tick
    // replaces it with the field of view it reaches (`setFovY` hands the same
    // value to the camera), which the turn and aim speeds that follow read.
    float fovY = 60.0f;
    std::function<void(float)> setFovY;
    // player_perspective_height.
    float perspectiveHeight = 1.0f;
    // get_debug_fast_bond_flag, which the turbo mode cheat sets.
    bool fastBond = false;
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
    // What the weapons' states reach out to; may be null.
    GunHooks* gunHooks = nullptr;
    // The watch; null stands for one that is closed.
    WatchHooks* watch = nullptr;
    // bonddead != 0, g_bondviewForceDisarm > 0 and the player flashing from a
    // hit (bondviewGetIfCurrentPlayerDamageShowTime).
    bool dead = false;
    bool forceDisarm = false;
    bool damageShowing = false;
    // g_ForceBondMoveOffset: the AI's scripted walk, which a frozen move
    // follows.
    Vec3 forcedMove{};
    // The tank hooks; null leaves the player on foot.
    TankHooks* tank = nullptr;
    // bondviewPlayerStopAudioForPause, run each tick the controls are locked.
    std::function<void()> stopAudioForPause;
    // field_2A08: the pitch the sight points at, in radians, which the tank's
    // gun follows.
    float sightPitch = 0.0f;
    // currentPlayerGetXAutoAimEnabled and ...Y; whether the gun has a target to
    // swing to on each axis (autoaim_target_x and _y) and where on the screen
    // (autoaimx and autoaimy).
    bool autoAimEnabled = false;
    bool autoAimTargetX = false;
    bool autoAimTargetY = false;
    float autoAimX = 0.0f;
    float autoAimY = 0.0f;
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
    // modelGetAnimFrame(&g_CurrentPlayer->model) >= modelGetAnimEndFrame: the
    // death animation has played out.
    virtual bool DeathAnimationFinished() const {
        return true;
    }
    // headlook and headup after that tick: the view's direction and up vector
    // in the player's frame. The standing pose looks straight ahead.
    virtual Vec3 Look() const {
        return { 0.0f, 0.0f, 1.0f };
    }
    virtual Vec3 Up() const {
        return { 0.0f, 1.0f, 0.0f };
    }
    // standbodyoffset, which headbodyoffset takes: how far the body sits from
    // the head at rest.
    virtual Vec3 BodyOffset() const {
        return { 0.0f, 0.0f, 0.0f };
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
    // cleanupSFXRelated: the tank's engine sounds stop, and bondview_r's
    // clearing of g_TankSfxState lets go of their handles. The same stop is
    // the tank part of bondviewPlayerStopAudioForPause.
    void StopTankSounds();
    void DetachTankSounds();

    // bondviewMovePlayerUpdateViewport's call to MoveBond: reads the
    // controller and moves the player through `stan`.
    void Tick(const MoveContext& context, const MoveInput& input, Stan& stan);
    // currentPlayerSetCameraMode: while a cinematic camera places the view
    // (`frozen`), the player's prop has no footprint.
    void SetCameraFrozen(bool frozen) {
        cameraFrozen_ = frozen;
    }
    // bondviewFrozenMoveBond's movement: the tick a cinematic camera mode runs
    // in place of MoveBond. The controller reads as untouched; the body is
    // held where it stands.
    void TickFrozen(const MoveContext& context, const MoveInput& input, Stan& stan);

    // The head motion each tick reads; null for the standing pose. Not owned.
    void SetHead(HeadMotion* head) {
        head_ = head;
    }
    // Whether the death animation has played out (the standing pose has none).
    bool DeathAnimationFinished() const {
        return head_ == nullptr || head_->DeathAnimationFinished();
    }
    // The player's prop, in `props`: walking keeps its position and tile, and
    // its room in the room lists, current. Not owned.
    void SetProp(PropList* props, int prop) {
        props_ = props;
        prop_ = prop;
    }
    // The player's prop in the prop list, -1 for none.
    int PropId() const {
        return prop_;
    }
    // bondviewGetPropHeightRelatedValues: the player's footprint, filling
    // `out`. False while the player's prop is hidden from collision tests.
    bool Footprint(PropCollider* out) const;
    // bondviewGetCollisionRadius.
    void CollisionRadius(float* radius, float* height, float* always30) const;

    // vv_theta and vv_verta, in degrees.
    float Theta() const {
        return theta_;
    }
    float Verta() const {
        return verta_;
    }
    // The watch's pause transition tilts the view (vv_verta) and sets the
    // speed the tilt moves at (speedverta).
    void SetVerta(float verta) {
        verta_ = verta;
    }
    void SetSpeedVerta(float speed) {
        speedVerta_ = speed;
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
    // field_3C4, field_3C8 and field_3CC: the smoothed camera position the
    // swirl orbits.
    const Vec3& SmoothedPosition() const {
        return smoothedScaled_;
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
    // headbodyoffset.
    Vec3 HeadBodyOffset() const {
        return head_ != nullptr ? head_->BodyOffset() : Vec3{ 0.0f, 0.0f, 0.0f };
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
    // The room of the portal tile, which bondviewGetCurrentPlayersRoom reads.
    int PortalRoom() const {
        return portalRoom_;
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
    // bondviewUpdateGuardTankFlagsRelated: the player's own prop and the tank
    // the player stands in or on stop being in the way of collision tests
    // (`collides` false) or are put back. A character's line tests and the
    // camera's call it around themselves.
    void SetCollides(bool collides);

    // isBondInTank.
    bool InTank() const {
        return inTank_;
    }
    // get_ptr_for_players_tank: the tank prop the player drives, -1 for none.
    int DrivenTank() const {
        return inTank_ ? playerTank_ : -1;
    }
    // g_PlayerTankProp: the tank the player sits in or stands on, -1 for none.
    int PlayerTankProp() const {
        return playerTank_;
    }
    // g_EnterTankAudioState.
    int TankRunState() const {
        return tankRunState_;
    }
    // g_ExplodeTankOnDeathFlag.
    bool ExplodeTankOnDeath() const {
        return explodeTankOnDeath_;
    }
    // bondviewSet3dCoord7F07CEB0: the direction the tank's gun points.
    Vec3 TankAimDirection() const;

    // bondshotspeed += (x, z): a hit pushes the player.
    void AddShotSpeed(float x, float z) {
        shotSpeed_[0] += x;
        shotSpeed_[2] += z;
    }
    // bondviewKillCurrentPlayer's saves: the view when the player was killed
    // (thetadie and vertadie) and the collision state (previous_collision_info),
    // which the death camera's replays start from.
    void BeginDeath() {
        thetaDie_ = theta_;
        vertaDie_ = verta_;
        deathState_ = { collisionPosition_, position_,    portalPosition_, tile_,        portalTile_, portalRoom_,
                        room_,              thetaTransform_, appliedView_, appliedView2_, collisionRadius_ };
        if (playerTank_ >= 0) {
            explodeTankOnDeath_ = true;
        }
    }
    // The part of bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP) that restarts a
    // death replay on foot: the player is put back where and as it was killed.
    void RestoreDeathState(Stan& stan);
    // field_488.collision_radius.
    float CollisionRadiusField() const {
        return collisionRadius_;
    }
    // field_3C4, field_3C8 and field_3CC: the smoothed position the death camera
    // looks at.
    const Vec3& SmoothedPosition() const {
        return smoothedScaled_;
    }
    float ThetaDie() const {
        return thetaDie_;
    }
    float VertaDie() const {
        return vertaDie_;
    }
    // bondviewGetPlayerDuckingHeightRelated.
    float DuckingHeightRelated() const {
        return eyeHeight_ + bounceOffset_ + duckingHeightOffset_;
    }

  private:
    // bondviewProcessInput.
    void ProcessInput(const MoveContext& context, const MoveInput& input, Stan& stan);
    // bondviewApplyVertaTheta.
    void ApplyVertaTheta();
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

    // The tank (bondview2.c).
    // B enters or leaves the tank.
    void UseTank();
    // bondviewTankModelRotationRelated.
    void UpdateTankModelOffset();
    // bondviewGetTankCollisionBounds for the tank the player drives, with
    // `position` as the seat.
    std::array<Vec2, 8> TankBounds(const Vec3& position, float orientation) const;
    // bondviewTankCollisionStatus: whether the tank fits at `position` on
    // `tile` turned to `orientation`. On a blocked line the segment is in
    // `lineA` and `lineB` when they are given.
    bool TankFits(Stan& stan, const Vec3& position, int tile, float orientation, Vec3* lineA, Vec3* lineB);
    // sub_GAME_7F07CDD4: the move of a running tank.
    bool TankTryMove(Stan& stan, const Vec3& position, int* tile);
    // The tank-turning share of MoveBond, and the forward share.
    void TurnTank(Stan& stan);
    void DriveTank(Stan& stan, Vec3* moveOffset);
    // bondviewCalcUpdatePlayerCollision's share for the world tank.
    bool UpdateWorldTank(Stan& stan);
    // The end of MoveBond's tank pass.
    void TickTankProp();
    // bondviewYPositionRelated for a tank the player stands in or on.
    float TankFloor(const Stan& stan) const;

    // The current tick's inputs from the rest of the game.
    MoveContext context_;
    MoveData move_;
    bool usePressed_ = false;
    bool cameraFrozen_ = false;
    // buttons_pressed and prev_buttons_pressed: the first and second
    // controller's buttons of the last tick.
    uint16_t buttonsPressed_ = 0;
    uint16_t prevButtons2_ = 0;

    // Look angles.
    float theta_ = 0;
    float thetaDie_ = 0;
    float vertaDie_ = 0;
    // previous_collision_info: what a death replay restores.
    struct DeathState {
        Vec3 collisionPosition{};
        Vec3 position{};
        Vec3 portalPosition{};
        int tile = kNoTile;
        int portalTile = kNoTile;
        int portalRoom = 0;
        int room = 0;
        Vec3 thetaTransform{};
        Vec3 appliedView{};
        Vec3 appliedView2{};
        float collisionRadius = 30.0f;
    };
    DeathState deathState_;
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
    int portalRoom_ = 0;
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

    // The tank. g_PlayerIsInTank, g_WorldTankProp (the tank the player bumped
    // into), g_PlayerTankProp (the one stood in or on) and g_PlayerTankYOffset,
    // g_BondCanEnterTank, g_EnterTankAudioState.
    bool inTank_ = false;
    int worldTank_ = -1;
    int playerTank_ = -1;
    float tankYOffset_ = 0.0f;
    bool canEnterTank_ = false;
    int tankRunState_ = kTankNotRunning;
    // g_TankTurretVerticalAngle and ...Related, g_TankTurretAngle,
    // g_TankTurretOrientationAngleRad and ...Deg, tank_turret_turn_speed,
    // g_TankOrientationAngle, g_TankTurnSpeed and g_TankTurretTurn.
    float tankTurretVertical_ = 0.0f;
    float tankTurretVerticalRelated_ = 0.0f;
    float tankTurretAngle_ = 0.0f;
    float tankTurretRad_ = 0.0f;
    float tankTurretDeg_ = 0.0f;
    float tankTurretTurnSpeed_ = 0.0f;
    float tankOrientation_ = 0.0f;
    float tankTurnSpeed_ = 0.0f;
    float tankTurretTurn_ = 0.0f;
    // g_TankEnteringSitHeight and ...Remain, g_TankEnterBondHorizAngleDeg and
    // ...VertAngleDeg, g_EnterTankCoord.
    float tankSitHeight_ = 0.0f;
    float tankSitRemain_ = 1.0f;
    float tankEnterTheta_ = 0.0f;
    float tankEnterVerta_ = 0.0f;
    Vec3 tankEnterCoord_{};
    // g_TankDamagePenaltyTicks, g_TankModelPositionOffset and
    // g_ExplodeTankOnDeathFlag.
    int tankDamagePenalty_ = 0;
    Vec3 tankModelOffset_{};
    bool explodeTankOnDeath_ = false;
    // g_TankSfxState (the engine and the tracks) and g_TankEngineSfxVolume.
    AudioDriver::SoundState* tankSound_[2] = { nullptr, nullptr };
    int32_t tankEngineVolume_ = 0;

    // The head: its position this tick, and the standing pose's height, which
    // sets the eye height so the first tick agrees with
    // bondviewPlayerBeginLife's.
    HeadMotion* head_ = nullptr;
    Vec3 headPosition_{};
    float standHeight_ = 168.0f;
    float eyeHeight_ = 175.0f;
};

} // namespace Frigate::Level
