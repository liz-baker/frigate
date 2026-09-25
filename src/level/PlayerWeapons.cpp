#include "level/PlayerWeapons.h"

namespace Frigate::Level {

namespace {

// ITEM_IDS values the weapon-change code names.
constexpr int32_t kItemSniperRifle = 17;
constexpr int32_t kItemBombCase = 33;

// The US timings (WHEN_5_SP188_INIT, WHEN_5_FLD8B0_SP and WHEN_8_SP178_INIT),
// in ticks. Single player only.
constexpr int32_t kLowerTicks = 0x10;
constexpr int32_t kSwapWaitTicks = 0x11;
constexpr int32_t kRaiseTicks = 0x17;

} // namespace

void PlayerWeapons::Init(std::shared_ptr<const WeaponTable> table, const Inventory* inventory) {
    *this = PlayerWeapons{};
    table_ = std::move(table);
    inventory_ = inventory;
}

void PlayerWeapons::GiveAmmo(int32_t ammoType, int32_t amount) {
    const int32_t weapon = WeaponId(HandRight);
    if (table_->AmmoType(weapon) == ammoType && table_->HasFlag(weapon, WeaponFlag::AmmoClipLimit)) {
        HandState& hand = hands_[HandRight];
        hand.ammoInMagazine += amount;
        if (table_->Item(weapon).magSize < hand.ammoInMagazine) {
            hand.ammoInMagazine = table_->Item(weapon).magSize;
        }
        ammoHeld_[static_cast<size_t>(ammoType)] = 0;
        return;
    }

    const int32_t maxAmmo = table_->MaxAmmo(ammoType);
    ammoHeld_[static_cast<size_t>(ammoType)] = maxAmmo < amount ? maxAmmo : amount;
}

void PlayerWeapons::EquipWeapon(Hand hand, int32_t next) {
    HandState& state = hands_[hand];
    state.currentAnimation = GunState::SwitchLower;
    state.nextWeapon = next;
    state.animationTrigger = 0;
}

int32_t PlayerWeapons::ItemInHandOrWatchMenu(Hand hand) const {
    const HandState& state = hands_[hand];
    return state.weaponNumWatchMenu >= 0 ? state.weaponNumWatchMenu : state.weaponNum;
}

bool PlayerWeapons::ItemAvailableForHand(int32_t right, int32_t left) const {
    if (equipAllGuns_ && right < kItemBombCase && right == left && table_->HasFlag(right, WeaponFlag::CanDualWield)) {
        return true;
    }
    return inventory_->HasDual(right, left);
}

bool PlayerWeapons::HandWithoutItem(Hand hand) const {
    return handInvisible_[hand] > 0 || (handItem_[hand] == kItemUnarmed && pendingItem_[hand] < 0);
}

void PlayerWeapons::PlaceItemInHand(Hand hand, int32_t item) {
    if (lockHandModel_[hand]) {
        return;
    }
    if (handInvisible_[hand] >= 0) {
        if (item != handItem_[hand]) {
            handInvisible_[hand] = -1;
            pendingItem_[hand] = item;
        }
        return;
    }
    if (item != handItem_[hand]) {
        pendingItem_[hand] = item;
        return;
    }
    handInvisible_[hand] = 1;
}

void PlayerWeapons::LoadHandModel(Hand hand) {
    if (handInvisible_[hand] < 0 && !lockHandModel_[hand]) {
        if (handInvisible_[hand] < -2 || handItem_[hand] == kItemUnarmed) {
            // TODO(https://github.com/liz-baker/frigate/issues/153): load the
            // item's first-person model into the hand (the pending item's
            // model file, when its record has one).
            handInvisible_[hand] = 1;
            handItem_[hand] = pendingItem_[hand];
            pendingItem_[hand] = -1;
        } else {
            handInvisible_[hand]--;
        }
    }
}

void PlayerWeapons::DetermineEquippedItem() {
    const int32_t current = WeaponId(HandRight);
    equipCurItem_ = kItemUnarmed;
    // TODO(https://github.com/liz-baker/frigate/issues/159): inventory entries
    // for props (bondinvGetTextbyInvIndex reads their text override).
    const auto& items = inventory_->Items();
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].IsDual() && items[i].right == current) {
            equipCurItem_ = static_cast<int>(i);
            return;
        }
    }
}

void PlayerWeapons::UnEquipWeapon(Hand hand, int32_t weapon) {
    HandState& state = hands_[hand];
    const int32_t previous = state.weaponNum;
    const int32_t ammoType = table_->AmmoType(previous);

    if (state.weaponNumWatchMenu < 0) {
        PlaceItemInHand(hand, weapon);
    }
    if (state.ammoInMagazine > 0) {
        ammoHeld_[static_cast<size_t>(ammoType)] += state.ammoInMagazine;
    }
    if (previous < kItemBombCase) {
        state.previousWeapon = previous;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/153): the multiplayer
    // hand reset (sub_GAME_7F09B368) and freeing the hand's rocket object
    // (sub_GAME_7F05FB00), which needs the prop list (#139).
    state.weaponNum = weapon;
    state.ammoInMagazine = 0;
    // TODO(https://github.com/liz-baker/frigate/issues/153): field_A4C and
    // field_A50, which the hand's firing effects read.
    DetermineEquippedItem();
}

void PlayerWeapons::TickHandState(Hand hand, int32_t triggerOn, int32_t clockTimer) {
    HandState& state = hands_[hand];
    int32_t item = ItemInHandOrWatchMenu(hand);

    state.holdTime = triggerOn;
    if (clockTimer > 0) {
        state.stateTime += clockTimer;
        state.stateUpdates += 1;
    }

    if (state.actionState == GunState::Idle) {
        // TODO(https://github.com/liz-baker/frigate/issues/153): a held
        // trigger enters the firing states, and an empty magazine with ammo
        // left starts a reload; the demo's triggers are not read yet.
        if (state.currentAnimation != 0) {
            state.actionState = state.currentAnimation;
            state.stateTime = 0;
            state.stateUpdates = 0;
        }
        state.currentAnimation = 0;
    }

    if (state.actionState == GunState::SwitchLower) {
        if (state.stateUpdates == 0) {
            state.swapWait = kSwapWaitTicks;
        }
        if (state.stateTime >= kLowerTicks) {
            ammoHeld_[static_cast<size_t>(table_->AmmoType(item))] += state.ammoInMagazine;
            state.ammoInMagazine = 0;
            // TODO(https://github.com/liz-baker/frigate/issues/153): freeing
            // the hand's rocket object (sub_GAME_7F05FB00).
            state.actionState = GunState::SwitchSwap;
            fistModel_ = inventory_->HasItem(kItemSniperRifle) ? kItemSniperRifle : kItemFist;
        }
        // TODO(https://github.com/liz-baker/frigate/issues/153): the lowering
        // hand's transform (field_8EC), which the gun renders with.
    }

    if (state.actionState == GunState::SwitchSwap || state.actionState == GunState::SwitchHold) {
        if (state.animationTrigger == 0 || state.stateTime >= state.swapWait) {
            if (state.actionState == GunState::SwitchSwap) {
                const HandState& other = hands_[1 - hand];
                if (other.actionState != GunState::SwitchSwap && other.actionState != GunState::SwitchLower &&
                    other.currentAnimation != GunState::SwitchLower && other.actionState != GunState::WatchLower &&
                    other.actionState != GunState::WatchSwap && other.actionState != GunState::WatchRaise &&
                    other.currentAnimation != GunState::WatchLower) {
                    if (hand == HandRight) {
                        if (!ItemAvailableForHand(state.nextWeapon, WeaponId(HandLeft))) {
                            EquipWeapon(HandLeft, kItemUnarmed);
                        }
                    } else if (!ItemAvailableForHand(WeaponId(HandRight), state.nextWeapon)) {
                        state.nextWeapon = kItemUnarmed;
                    }
                }
                UnEquipWeapon(hand, state.nextWeapon);
                item = ItemInHandOrWatchMenu(hand);
                state.actionState = GunState::SwitchHold;
            } else if (HandWithoutItem(hand)) {
                state.actionState = GunState::SwitchRaise;
                state.stateTime = 0;
                state.stateUpdates = 0;
            }
        }
    }

    if (state.actionState == GunState::SwitchRaise) {
        // TODO(https://github.com/liz-baker/frigate/issues/153): at the start of
        // the raise, the pickup sound, sub_GAME_7F0649D8 and the trigger
        // release flag.
        const int32_t modelItem = item == kItemFist ? fistModel_ : item;
        if (state.stateTime >= kRaiseTicks || !table_->Item(modelItem).hasHeader ||
            !table_->HasFlag(item, WeaponFlag::ShowFirstPerson) ||
            table_->HasFlag(item, WeaponFlag::HideFirstPersonHand)) {
            state.actionState = GunState::Idle;
            state.stateTime = 0;
            state.stateUpdates = 0;
        }
    }
}

void PlayerWeapons::Tick(int32_t clockTimer) {
    TickHandState(HandRight, 0, clockTimer);
    TickHandState(HandLeft, 0, clockTimer);
    LoadHandModel(HandRight);
    LoadHandModel(HandLeft);
}

} // namespace Frigate::Level
