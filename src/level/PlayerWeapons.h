#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include "level/Inventory.h"
#include "level/Matrix.h"
#include "level/WeaponTable.h"

namespace Frigate::Model {
class ModelInstance;
} // namespace Frigate::Model

namespace Frigate::Level {

class ModelFactory;

// GUNHAND.
enum Hand : int { HandRight = 0, HandLeft = 1 };

// GUN_ANIM_STATE_* values that the weapon-change states use.
namespace GunState {
constexpr int32_t Idle = 0;
constexpr int32_t TriggerPress = 1;
constexpr int32_t SwitchLower = 5;
constexpr int32_t SwitchSwap = 6;
constexpr int32_t SwitchHold = 7;
constexpr int32_t SwitchRaise = 8;
constexpr int32_t WatchLower = 14;
constexpr int32_t WatchSwap = 15;
constexpr int32_t WatchRaise = 16;
} // namespace GunState

// The player's screen rectangle and camera scale, as the gun code reads them
// (getPlayer_c_screenleft and the rest, c_scalex and c_scaley).
struct GunView {
    float screenLeft = 0.0f;
    float screenTop = 0.0f;
    float screenWidth = 320.0f;
    float screenHeight = 240.0f;
    float scaleX = 0.0f;
    float scaleY = 0.0f;
};

// The parts of `struct hand` that the weapon-change states and the gun's pose
// read and write.
struct HandState {
    // weaponnum and weaponnum_watchmenu (-1 when the watch menu holds nothing).
    int32_t weaponNum = kItemUnarmed;
    int32_t weaponNumWatchMenu = -1;
    int32_t previousWeapon = kItemUnarmed;
    // weapon_hold_time: the trigger, as gunTickHandState last saw it.
    int32_t holdTime = 0;
    // weapon_action_state.
    int32_t actionState = GunState::Idle;
    // weapon_current_animation: the state to enter when the hand next idles.
    int32_t currentAnimation = 0;
    int32_t ammoInMagazine = 0;
    int32_t nextWeapon = kItemUnarmed;
    int32_t animationTrigger = 0;
    // field_890 and field_88C: ticks and updates spent in the action state.
    int32_t stateTime = 0;
    int32_t stateUpdates = 0;
    // field_8B0: the state time a requested change waits for before it swaps.
    int32_t swapWait = 0;
    // field_8B8: the direction the change cycles in.
    int32_t cycleDirection = 0;
    // numvisibleshells: the shotgun shells shown in the holder.
    int32_t visibleShells = 0;

    // field_92C and field_8EC: a state's animation offsets the gun with
    // `transform`.
    bool hasTransform = false;
    Mtxf transform = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };

    // weapon_firing_status and field_87D: the hand is firing, and its muzzle
    // flash shows.
    int8_t firingStatus = 0;
    bool flashVisible = false;
    // field_87E: the value the switches sub_GAME_7F05EA94 sets take.
    bool switchesOn = true;
    // field_87F: the hand's model is drawn.
    bool modelShown = false;

    // The gun sway (bgunCalculateBlend and gunSetBondWeaponSway): the four
    // blend targets the sway interpolates between, and the state that picks
    // them.
    int32_t curBlendPos = 0;
    float dampt = 0.0f;
    float blendScale1 = 1.0f;
    int32_t sideFlag = 0;
    std::array<Vec3, 4> blendPos{};
    std::array<Vec3, 4> blendLook = { { { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 } } };
    std::array<Vec3, 4> blendUp = { { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } } };
    // weapon_theta_displacement and weapon_verta_displacement.
    float thetaDisplacement = 0.0f;
    float vertaDisplacement = 0.0f;
    // gunofs2_x, gunofs2_y and gunofs2_z.
    Vec3 offset2{};
    // field_A34: how far the gun has moved to its side of the screen for dual
    // wielding, 0 to 2 (right hand) or 0 to -2 (left).
    float dualShift = 0.0f;
    // field_A38, field_A3C and field_A40: the point the gun aims at, in view space.
    Vec3 aim = { 0.0f, 0.0f, 1000.0f };
    // spring_pos, spring_look and spring_up, and the sway_ vectors they scale to.
    Vec3 springPos{};
    Vec3 springLook = { 0.0f, 0.0f, -19.999996f };
    Vec3 springUp = { 0.0f, 19.999996f, 0.0f };
    Vec3 swayPos{};
    Vec3 swayLook = { 0.0f, 0.0f, -1.0f };
    Vec3 swayUp = { 0.0f, 1.0f, 0.0f };

    // field_A84: the sight or hinge's angle, which grows while the trigger is
    // held. field_A88 and field_A8C: how far the slide has been thrown back,
    // and whether it is still travelling back.
    float hingeAngle = 0.0f;
    float slideOffset = 0.0f;
    bool slideRecoiling = false;
};

// The parts of `struct player` that the gun code keeps beside the hands.
struct GunAimState {
    // aimtype.
    int32_t aimType = 0;
    // crosshair_x_pos, crosshair_y_pos, guncrossdamp: the crosshair's smoothed
    // stick position and the damping it is smoothed with.
    float crosshairX = 0.0f;
    float crosshairY = 0.0f;
    float crosshairDamp = 0.9f;
    // crosshair_angle: the crosshair on screen.
    float crosshairScreenX = 0.0f;
    float crosshairScreenY = 0.0f;
    // gun_azimuth_angle, gun_azimuth_turning and gunaimdamp: the same for the
    // gun's aim.
    float gunAzimuth = 0.0f;
    float gunTurning = 0.0f;
    float gunAimDamp = 0.9f;
    // field_FFC: where the gun points on screen.
    float aimScreenX = 0.0f;
    float aimScreenY = 0.0f;
};

// What the current player carries and holds: the hands, the ammo held, and the
// held-gun state (the parts of `struct player` initBONDdata and the gun code
// keep for them).
class PlayerWeapons {
  public:
    using RandomFn = std::function<uint32_t()>;

    // init_player_BONDdata_stats's part for the hands. `inventory` is the
    // player's and must outlive this. `random` is randomGetNext, which the
    // sway's first blends draw from.
    void Init(std::shared_ptr<const WeaponTable> table, const Inventory* inventory, const RandomFn& random);

    // Where the hands' models come from; not owned. Without one the hands track
    // which item they hold but have no model to draw.
    void SetModels(ModelFactory* models) {
        models_ = models;
    }

    // False until Init has been given a table.
    bool Ready() const {
        return table_ != nullptr;
    }
    const WeaponTable& Table() const {
        return *table_;
    }

    // give_cur_player_ammo.
    void GiveAmmo(int32_t ammoType, int32_t amount);
    int32_t AmmoHeld(int32_t ammoType) const {
        return ammoHeld_[static_cast<size_t>(ammoType)];
    }

    // currentPlayerEquipWeaponWrapper: asks the hand to change to `next`,
    // which gunTickHandState carries out.
    void EquipWeapon(Hand hand, int32_t next);
    // currentPlayerUnEquipWeaponWrapper.
    void UnEquipWeapon(Hand hand, int32_t weapon);
    // draw_item_in_hand: asks the hand to lower what it holds and take `item`
    // into the watch menu's slot (the watch states of gunTickHandState).
    void DrawItemInHand(Hand hand, int32_t item);
    // sub_GAME_7F05DAE4: the hand goes back to its own weapon from the watch
    // menu's item.
    void RestoreFromWatchMenu(Hand hand);

    // The changing-weapon part of gunTick: gunTickHandState for both hands with
    // the triggers released, then used_to_load_1st_person_model_on_demand.
    void Tick(int32_t clockTimer);

    // gunSetBondWeaponSway. `breathing` is bheadGetBreathingValue and
    // `bondBreathing` bondviewGetBondBreathing; `maxSpeed`, `vertaSpeed` and
    // `thetaSpeed` are the player's movement and turn speeds.
    void SetBondWeaponSway(float breathing, float bondBreathing, float maxSpeed, float vertaSpeed, float thetaSpeed,
                           int32_t clockTimer, float globalTimerDelta, const RandomFn& random);
    // gunSetOffsetRelated: `verta` is the look pitch in radians.
    void SetOffsetRelated(float verta);
    // gunSetAimType.
    void SetAimType(int32_t type) {
        aim_.aimType = type;
    }
    // sub_GAME_7F067F58 with the default aim-lock speed: the crosshair and gun
    // aim follow the turn (the swivelling control styles).
    void AimSwivel(float turnX, float turnY, int32_t clockTimer, const GunView& view);
    // sub_GAME_7F067FBC: the crosshair and gun aim follow the stick (manual aim).
    void AimManual(float turnX, float turnY, int32_t clockTimer, const GunView& view);

    // getCurrentPlayerWeaponId.
    int32_t WeaponId(Hand hand) const {
        return hands_[hand].weaponNum;
    }
    // get_item_in_hand_or_watch_menu.
    int32_t ItemInHandOrWatchMenu(Hand hand) const;
    // bondwalkItemCheckBitflags for the item in the right hand.
    bool RightHandHasFlag(uint32_t mask) const {
        return table_->HasFlag(hands_[HandRight].weaponNum, mask);
    }
    const HandState& Hands(Hand hand) const {
        return hands_[hand];
    }
    // The hand as the gun's pose writes it.
    HandState& MutableHand(Hand hand) {
        return hands_[hand];
    }
    const GunAimState& Aim() const {
        return aim_;
    }
    // equipcuritem: the inventory index of the right hand's weapon.
    int EquippedIndex() const {
        return equipCurItem_;
    }
    // Gun_hand_without_item.
    bool HandWithoutItem(Hand hand) const;
    // The item whose model the hand holds (hand_item).
    int32_t HandItem(Hand hand) const {
        return handItem_[hand];
    }
    // get_ptr_weapon_model_header_line: whether `item` has a model record. The
    // fist takes the model cur_item_weapon_getname names.
    bool ItemHasModel(int32_t item) const {
        return table_->Item(item == kItemFist ? fistModel_ : item).hasHeader;
    }
    // cur_item_weapon_getname: the item whose model the fist slot shows.
    int32_t FistModel() const {
        return fistModel_;
    }
    // copy_of_body_obj_header for the hand: the model it last loaded.
    const std::shared_ptr<Model::ModelInstance>& HandModel(Hand hand) const {
        return handModel_[hand];
    }
    // gunposamplitude, the sway's size.
    float SwayAmplitude() const {
        return gunPosAmplitude_;
    }
    // tileColor: the shade of the tile the player stands on, red in the top
    // byte, which the gun is lit with.
    uint32_t TileColour() const {
        return tileColour_;
    }

  private:
    // bondinvItemAvailableForHand.
    bool ItemAvailableForHand(int32_t right, int32_t left) const;
    // place_item_in_hand_swap_and_make_visible.
    void PlaceItemInHand(Hand hand, int32_t item);
    // used_to_load_1st_person_model_on_demand.
    void LoadHandModel(Hand hand);
    // gunTickHandState, for the states that change weapons.
    void TickHandState(Hand hand, int32_t triggerOn, int32_t clockTimer);
    // sub_GAME_7F0649D8: fills the magazine when a weapon is raised.
    void FillMagazine(Hand hand);
    // get_ammo_in_hands_weapon.
    int32_t AmmoInHandsWeapon(Hand hand) const;
    // bondinvDetermineEquippedItem.
    void DetermineEquippedItem();
    // bgunCalculateBlend.
    void CalculateBlend(Hand hand, const RandomFn& random);
    // caclulate_gun_crosshair_position_rotation.
    void AimCrosshair(float turnX, float turnY, float crosshairDamp, float aimDamp, int32_t clockTimer,
                      const GunView& view);

    std::shared_ptr<const WeaponTable> table_;
    ModelFactory* models_ = nullptr;
    const Inventory* inventory_ = nullptr;
    std::array<HandState, 2> hands_{};
    std::array<int32_t, 30> ammoHeld_{};
    // hand_invisible, hand_item, field_2A44 (the item waiting to be loaded, -1
    // for none) and lock_hand_model.
    std::array<int32_t, 2> handInvisible_{ 0, 0 };
    std::array<int32_t, 2> handItem_{ kItemUnarmed, kItemUnarmed };
    std::array<int32_t, 2> pendingItem_{ -1, -1 };
    std::array<bool, 2> lockHandModel_{ false, false };
    // The models the hands hold (copy_of_body_obj_header).
    std::array<std::shared_ptr<Model::ModelInstance>, 2> handModel_;
    // cur_item_weapon_getname: the model the fist slot shows.
    int32_t fistModel_ = kItemFist;
    int equipCurItem_ = 0;
    // equipallguns: the cheat that makes every gun available.
    bool equipAllGuns_ = false;

    // gunposamplitude, field_1080 and field_107C, syncoffset, synccount,
    // syncchange and gunsync: the sway's amplitude, its smoothed inputs and
    // the drift that keeps the two hands out of step.
    float gunPosAmplitude_ = 1.0f;
    float amplitudeSum_ = 0.0f;
    float breathingSum_ = 0.0f;
    int32_t syncOffset_ = 0;
    float syncCount_ = 0.0f;
    float syncChange_ = 0.0f;
    float gunSync_ = 0.0f;
    GunAimState aim_;
    // TODO(https://github.com/liz-baker/frigate/issues/192): the gun tick blends
    // this towards the floor tile's colour each frame; it stays at the initial white.
    uint32_t tileColour_ = 0xFFFFFF00;
};

} // namespace Frigate::Level
