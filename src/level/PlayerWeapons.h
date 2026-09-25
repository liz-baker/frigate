#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "level/Inventory.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

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

// The parts of `struct hand` that changing weapons reads and writes.
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
};

// What the current player carries and holds: the hands, the ammo held, and the
// held-gun model state (the parts of `struct player` initBONDdata and the gun
// code keep for them).
class PlayerWeapons {
  public:
    // init_player_BONDdata_stats's part for the hands. `inventory` is the
    // player's and must outlive this.
    void Init(std::shared_ptr<const WeaponTable> table, const Inventory* inventory);

    // False until Init has been given a table.
    bool Ready() const {
        return table_ != nullptr;
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

    // The changing-weapon part of gunTick: gunTickHandState for both hands with
    // the triggers released, then used_to_load_1st_person_model_on_demand.
    void Tick(int32_t clockTimer);

    // getCurrentPlayerWeaponId.
    int32_t WeaponId(Hand hand) const {
        return hands_[hand].weaponNum;
    }
    // bondwalkItemCheckBitflags for the item in the right hand.
    bool RightHandHasFlag(uint32_t mask) const {
        return table_->HasFlag(hands_[HandRight].weaponNum, mask);
    }
    const HandState& Hands(Hand hand) const {
        return hands_[hand];
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

  private:
    // get_item_in_hand_or_watch_menu.
    int32_t ItemInHandOrWatchMenu(Hand hand) const;
    // bondinvItemAvailableForHand.
    bool ItemAvailableForHand(int32_t right, int32_t left) const;
    // place_item_in_hand_swap_and_make_visible.
    void PlaceItemInHand(Hand hand, int32_t item);
    // used_to_load_1st_person_model_on_demand.
    void LoadHandModel(Hand hand);
    // gunTickHandState, for the states that change weapons.
    void TickHandState(Hand hand, int32_t triggerOn, int32_t clockTimer);
    // bondinvDetermineEquippedItem.
    void DetermineEquippedItem();

    std::shared_ptr<const WeaponTable> table_;
    const Inventory* inventory_ = nullptr;
    std::array<HandState, 2> hands_{};
    std::array<int32_t, 30> ammoHeld_{};
    // hand_invisible, hand_item, field_2A44 (the item waiting to be loaded, -1
    // for none) and lock_hand_model.
    std::array<int32_t, 2> handInvisible_{ 0, 0 };
    std::array<int32_t, 2> handItem_{ kItemUnarmed, kItemUnarmed };
    std::array<int32_t, 2> pendingItem_{ -1, -1 };
    std::array<bool, 2> lockHandModel_{ false, false };
    // cur_item_weapon_getname: the model the fist slot shows.
    int32_t fistModel_ = kItemFist;
    int equipCurItem_ = 0;
    // equipallguns: the cheat that makes every gun available.
    bool equipAllGuns_ = false;
};

} // namespace Frigate::Level
