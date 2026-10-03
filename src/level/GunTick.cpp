#include <algorithm>
#include <cmath>

#include "game/Sound.h"
#include "level/GunKeyframes.h"
#include "level/PlayerWeapons.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

// The US timings (the WHEN_* macros of gunTickHandState), in ticks. Single
// player only: the multiplayer variants of the switch and raise times are
// not ported.
constexpr int32_t kRugerFireTicks = 6;
constexpr int32_t kGrenadeHoldTicks = 0xF0;
constexpr int32_t kDryFireTicks = 0x14;
constexpr int32_t kLowerTicks = 0x10;
constexpr int32_t kSwapWaitTicks = 0x11;
constexpr int32_t kRaiseTicks = 0x17;
constexpr int32_t kReloadLowerTicks = 0x10;
constexpr int32_t kReloadSwapWait = 0x11;
constexpr int32_t kReloadRaiseTicks = 0x17;
constexpr int32_t kWatchLowerTicks = 0x10;
constexpr int32_t kWatchRaiseTicks = 0x17;
constexpr int32_t kKnifeStrikeTicks = 0x10;
constexpr int32_t kKnifeRecoverTicks = 0x18;
constexpr int32_t kPunchRecoverTicks = 0x1E;
// The divisors the reload and watch states tilt the gun by (un_f32_div_1 and
// un_f32_div_2).
constexpr float kLowerDivisor = 16.0f;
constexpr float kRaiseDivisor = 23.0f;
// DUAL_WIELD_TRIGGER_SWAP_TICKS and DUAL_WIELD_SINGLE_TRIGGER_SWAP_TICKS.
constexpr int32_t kDualWieldSwapTicks = 20;
constexpr int32_t kDualWieldSingleSwapTicks = 30;

constexpr float kPi = 3.1415927f;
constexpr float kHalfPi = 1.5707964f;
constexpr float kTau = 6.2831855f;
// M_LN2F, in radians: the angle the gun is tilted to at the bottom of a weapon
// change.
constexpr float kLn2 = 0.69813174f;
// The twist the reload states give the gun (un_f32_num).
constexpr float kReloadTwist = 0.0f;

// The sound of raising `item` (the switch of the raise state): none for the
// items that are not held as a gun.
AudioDriver::SfxId PickupSound(int32_t item) {
    switch (item) {
        case kItemLaser:
            return AudioDriver::kSfxPickupLaser;
        case kItemKnife:
        case kItemThrowKnife:
            return AudioDriver::kSfxPickupKnife;
        case kItemTimedMine:
        case kItemProximityMine:
        case kItemRemoteMine:
            return AudioDriver::kSfxPickupMine;
        case kItemUnarmed:
        case kItemFist:
        case kItemWatchLaser:
        case kItemGrenade:
        case kItemTrigger:
        case kItemTaser:
        case kItemTankShells:
        case kItemBombCase:
        case kItemPlastique:
        case kItemCamera:
        case kItemBug:
        case kItemMicroCamera:
        case kItemWatchMagnetAttract:
        case kItemGoldenEyeKey:
        case kItemToken:
            return AudioDriver::kSfxNothing;
        default:
            return AudioDriver::kSfxPickupGun;
    }
}

// Whether the reload of `item` cocks: every item but the ones that are not
// held as a gun.
bool CocksOnReload(int32_t item) {
    switch (item) {
        case kItemUnarmed:
        case kItemFist:
        case kItemKnife:
        case kItemThrowKnife:
        case kItemLaser:
        case kItemWatchLaser:
        case kItemGrenade:
        case kItemTimedMine:
        case kItemProximityMine:
        case kItemRemoteMine:
        case kItemTrigger:
        case kItemTaser:
        case kItemTankShells:
        case kItemBombCase:
        case kItemPlastique:
        case kItemCamera:
        case kItemBug:
        case kItemMicroCamera:
        case kItemWatchMagnetAttract:
        case kItemGoldenEyeKey:
        case kItemToken:
            return false;
        default:
            return true;
    }
}

// The tilt every weapon-change state applies to the gun: a rotation about X by
// `angle` that also pulls the gun down and forward.
void SetChangeTransform(HandState& hand, float angle) {
    hand.hasTransform = true;
    hand.transform = RotationX(angle);
    hand.transform.m[3][0] = 0.0f;
    hand.transform.m[3][1] = (1.0f - GameCosf(angle)) * -60.0f;
    hand.transform.m[3][2] = GameSinf(angle) * 15.0f;
}

// The reload states' tilt: a twist about Z, then the tilt about X, pulling the
// gun down by sub_GAME_7F0649AC.
void SetReloadTransform(HandState& hand, Hand handNum, float angle, float twistDivisor, int32_t item) {
    hand.hasTransform = true;
    const float twist = kReloadTwist / twistDivisor;
    hand.transform = RotationZ(handNum == HandRight ? twist : -twist);
    hand.transform = Multiply(RotationX(angle), hand.transform);
    hand.transform.m[3][0] = 0.0f;
    hand.transform.m[3][1] = (item == kItemRocketLauncher ? -80.0f : -60.0f) * (1.0f - GameCosf(angle));
    hand.transform.m[3][2] = GameSinf(angle) * 15.0f;
}

// sub_GAME_7F06D0CC: moves the angle `from` towards `to` by `fraction` of the
// shorter way round the circle.
float LerpAngle(float from, float to, float fraction) {
    float value = to - from;
    if (to < from) {
        value += kTau;
    }
    if (value < kPi) {
        from += value * fraction;
        if (from >= kTau) {
            from -= kTau;
        }
    } else {
        from -= (kTau - value) * fraction;
        if (from < 0) {
            from += kTau;
        }
    }
    return from;
}

} // namespace

bool PlayerWeapons::ItemAvailable(int32_t item) const {
    if (equipAllGuns_ && item != kItemUnarmed && item < kItemBombCase) {
        return true;
    }
    return inventory_->HasItem(item);
}

bool PlayerWeapons::ItemHasAmmo(int32_t item) const {
    if (table_->HasFlag(item, WeaponFlag::HasAmmo)) {
        const int32_t type = table_->AmmoType(item);
        if (type == 0 || ammoHeld_[static_cast<size_t>(type)] > 0) {
            return true;
        }
    }
    return false;
}

bool PlayerWeapons::CanUseWeapon(Hand hand) const {
    return table_->AmmoType(WeaponId(hand)) == 0 || hands_[hand].ammoInMagazine > 0;
}

int32_t PlayerWeapons::NextWeaponInCycle(Hand hand, int32_t direction) const {
    const HandState& state = hands_[hand];
    if (state.actionState == GunState::SwitchLower) {
        if ((direction < 0 && state.cycleDirection > 0) || (direction > 0 && state.cycleDirection < 0)) {
            return WeaponId(hand);
        }
        return state.nextWeapon;
    }
    if (state.actionState == GunState::SwitchSwap) {
        return state.nextWeapon;
    }
    return WeaponId(hand);
}

void PlayerWeapons::RequestHandWeaponChange(Hand hand, int32_t next, int32_t cycleDirection) {
    HandState& state = hands_[hand];
    if (state.actionState == GunState::SwitchLower || state.actionState == GunState::SwitchSwap) {
        state.swapWait = state.stateTime + kSwapWaitTicks;
    }
    if (NextWeaponInCycle(hand, 0) != next) {
        if (state.actionState != GunState::SwitchLower && state.actionState != GunState::SwitchSwap) {
            state.currentAnimation = GunState::SwitchLower;
        }
        state.nextWeapon = next;
        state.animationTrigger = 1;
        state.cycleDirection = cycleDirection;
    }
}

void PlayerWeapons::CycleForward(int32_t* right, int32_t* left, bool requireAmmo) const {
    int32_t weapon1 = *right;
    int32_t weapon2 = *left;
    const std::vector<InvItem>& items = inventory_->Items();

    // The game walks the circular list until it comes back to its start, taking
    // the first entry after the current pair, and wraps once more when ammo is
    // not required.
    size_t visited = 0;
    for (size_t i = 0; i < items.size() && visited <= 2 * items.size(); visited++) {
        const InvItem& item = items[i];
        if (!item.IsProp() && !item.IsDual()) {
            if (item.right < kItemBombCase && item.right > weapon1) {
                if (!requireAmmo || ItemHasAmmo(item.right)) {
                    weapon1 = item.right;
                    weapon2 = 0;
                    break;
                }
            }
        } else if (item.IsDual()) {
            if (item.right > weapon1 || (weapon1 == item.right && item.left > weapon2)) {
                if (!requireAmmo || ItemHasAmmo(item.right) || ItemHasAmmo(item.left)) {
                    weapon1 = item.right;
                    weapon2 = item.left;
                    break;
                }
            }
        }

        i++;
        if (i == items.size()) {
            i = 0;
            if (requireAmmo) {
                break;
            }
            weapon1 = -1;
            weapon2 = -1;
        }
    }

    if (equipAllGuns_) {
        int32_t candidate = *right;
        if (table_->HasFlag(*right, WeaponFlag::CanDualWield) && *left < *right &&
            (!requireAmmo || ItemHasAmmo(*right)) && (weapon1 != *right || *right < weapon2)) {
            weapon1 = *right;
            weapon2 = *right;
        } else if (weapon1 != *right || weapon2 == *left) {
            do {
                candidate = (candidate + 1) % kItemBombCase;
                if (candidate == kItemUnarmed) {
                    candidate = (candidate + 1) % kItemBombCase;
                }
                if (!requireAmmo || ItemHasAmmo(candidate)) {
                    weapon1 = candidate;
                    weapon2 = kItemUnarmed;
                    break;
                }
            } while (candidate != weapon1);
        }
    }

    *right = weapon1;
    *left = weapon2;
}

void PlayerWeapons::CycleBackward(int32_t* right, int32_t* left, bool requireAmmo) const {
    int32_t weapon1 = *right;
    int32_t weapon2 = *left;
    const std::vector<InvItem>& items = inventory_->Items();

    if (!items.empty()) {
        // The walk starts at the entry before the first and goes backwards.
        size_t i = items.size() - 1;
        for (size_t visited = 0; visited <= 2 * items.size(); visited++) {
            const InvItem& item = items[i];
            if (!item.IsProp() && !item.IsDual()) {
                if (item.right < kItemBombCase && (item.right < weapon1 || (weapon1 == item.right && weapon2 > 0))) {
                    if (!requireAmmo || ItemHasAmmo(item.right)) {
                        weapon1 = item.right;
                        weapon2 = kItemUnarmed;
                        break;
                    }
                }
            } else if (item.IsDual()) {
                if (item.right < weapon1 || (weapon1 == item.right && item.left < weapon2)) {
                    if (!requireAmmo || ItemHasAmmo(item.right) || ItemHasAmmo(item.left)) {
                        weapon1 = item.right;
                        weapon2 = item.left;
                        break;
                    }
                }
            }

            if (i == 0) {
                if (requireAmmo) {
                    break;
                }
                weapon1 = 1000;
                weapon2 = 1000;
                i = items.size() - 1;
            } else {
                i--;
            }
        }
    }

    if (equipAllGuns_) {
        int32_t candidate = *right;
        if (*left == kItemUnarmed) {
            candidate = (candidate + kItemBombCase - 1) % kItemBombCase;
            if (candidate == kItemUnarmed) {
                candidate = (candidate + kItemBombCase - 1) % kItemBombCase;
            }
        }
        // Bounded: every step moves the candidate back one weapon.
        for (int32_t steps = 0; steps < 2 * kItemBombCase; steps++) {
            if (candidate == weapon1) {
                if (table_->HasFlag(candidate, WeaponFlag::CanDualWield) && (!requireAmmo || ItemHasAmmo(candidate)) &&
                    (candidate != *right || candidate < *left) && weapon2 < candidate) {
                    weapon1 = candidate;
                    weapon2 = candidate;
                }
                break;
            }
            if (!requireAmmo || ItemHasAmmo(candidate)) {
                if (table_->HasFlag(candidate, WeaponFlag::CanDualWield) &&
                    (candidate != *right || candidate < *left)) {
                    weapon1 = candidate;
                    weapon2 = candidate;
                } else {
                    weapon1 = candidate;
                    weapon2 = kItemUnarmed;
                }
                break;
            }
            candidate = (candidate + kItemBombCase - 1) % kItemBombCase;
            if (candidate == kItemUnarmed) {
                candidate = (candidate + kItemBombCase - 1) % kItemBombCase;
            }
        }
    }

    *right = weapon1;
    *left = weapon2;
}

void PlayerWeapons::AutoAdvanceOnDepleteAllAmmo() {
    int32_t nextRight = NextWeaponInCycle(HandRight, 1);
    int32_t dupeRight = nextRight;
    int32_t nextLeft = NextWeaponInCycle(HandLeft, 1);
    int32_t dupeLeft = nextLeft;

    if (dupeRight >= kItemBombCase || dupeLeft >= kItemBombCase) {
        dupeRight = hands_[HandRight].previousWeapon;
        dupeLeft = hands_[HandLeft].previousWeapon;
    } else if (dupeRight == kItemRemoteMine && ItemAvailable(kItemTrigger)) {
        dupeRight = kItemTrigger;
        dupeLeft = kItemUnarmed;
    } else {
        CycleForward(&dupeRight, &dupeLeft, true);
        if (dupeRight < nextRight || (dupeRight == nextRight && nextLeft >= dupeLeft)) {
            dupeRight = nextRight;
            dupeLeft = nextLeft;
            CycleBackward(&dupeRight, &dupeLeft, true);
        }
    }

    RequestHandWeaponChange(HandRight, dupeRight, 1);
    RequestHandWeaponChange(HandLeft, dupeLeft, 1);
}

void PlayerWeapons::AdvanceThroughInventory() {
    int32_t nextRight = NextWeaponInCycle(HandRight, 1);
    int32_t nextLeft = NextWeaponInCycle(HandLeft, 1);
    if (nextRight >= kItemBombCase || nextLeft >= kItemBombCase) {
        nextRight = hands_[HandRight].previousWeapon;
        nextLeft = hands_[HandLeft].previousWeapon;
    } else {
        CycleForward(&nextRight, &nextLeft, false);
    }
    RequestHandWeaponChange(HandRight, nextRight, 1);
    RequestHandWeaponChange(HandLeft, nextLeft, 1);
}

void PlayerWeapons::BackstepThroughInventory() {
    int32_t nextRight = NextWeaponInCycle(HandRight, -1);
    int32_t nextLeft = NextWeaponInCycle(HandLeft, -1);
    if (nextRight >= kItemBombCase || nextLeft >= kItemBombCase) {
        nextRight = hands_[HandRight].previousWeapon;
        nextLeft = hands_[HandLeft].previousWeapon;
    } else {
        CycleBackward(&nextRight, &nextLeft, false);
    }
    RequestHandWeaponChange(HandRight, nextRight, -1);
    RequestHandWeaponChange(HandLeft, nextLeft, -1);
}

void PlayerWeapons::DetonateRemoteMines() {
    hands_[HandRight].actionState = GunState::Idle;
    hands_[HandRight].currentAnimation = 0;
    TriggerRemoteMines();
}

void PlayerWeapons::ReleaseRocket(Hand hand) {
    HandState& state = hands_[hand];
    if (state.rocketProp >= 0) {
        if (freeRocket_) {
            freeRocket_(hand);
        }
        state.rocketProp = -1;
        state.rocketModel.reset();
    }
}

void PlayerWeapons::TriggerRemoteMines() {
    remoteMinesTriggered_ = true;
    Game::Sound::PlaySfx(AudioDriver::kSfxWatchDetonateMine);
}

void PlayerWeapons::AttemptReload(Hand hand) {
    if (table_->AmmoType(WeaponId(hand)) != 0 && hands_[hand].currentAnimation == 0) {
        hands_[hand].currentAnimation = GunState::ReloadStart;
    }
}

float PlayerWeapons::ItemZoom() const {
    const int32_t item = ItemInHandOrWatchMenu(HandRight);
    if (item == kItemSniperRifle) {
        return sniperZoom_;
    }
    if (item == kItemCamera) {
        return cameraZoom_;
    }
    return table_->Item(item).zoom;
}

void PlayerWeapons::ZoomOut(float zoom) {
    const int32_t item = ItemInHandOrWatchMenu(HandRight);
    float* target = item == kItemSniperRifle ? &sniperZoom_ : item == kItemCamera ? &cameraZoom_ : nullptr;
    if (target != nullptr) {
        *target *= 1.0f + (zoom * 0.1f);
        if (*target > 60.0f) {
            *target = 60.0f;
        }
    }
}

void PlayerWeapons::ZoomIn(float zoom) {
    const int32_t item = ItemInHandOrWatchMenu(HandRight);
    float* target = item == kItemSniperRifle ? &sniperZoom_ : item == kItemCamera ? &cameraZoom_ : nullptr;
    if (target != nullptr) {
        *target /= 1.0f + (zoom * 0.1f);
        if (*target < 7.0f) {
            *target = 7.0f;
        }
    }
}

void PlayerWeapons::RemoveItemInHand(Hand hand) {
    handInvisible_[hand] = 0;
    handItem_[hand] = kItemUnarmed;
    pendingItem_[hand] = -1;
    lockHandModel_[hand] = true;
}

void PlayerWeapons::FillMagazine(Hand hand) {
    HandState& state = hands_[hand];
    const int32_t item = WeaponId(hand);
    const int32_t inMagazine = state.ammoInMagazine;
    const int32_t inHands = AmmoInHandsWeapon(hand);
    const ItemStats& stats = table_->Item(item);
    const int32_t total = inHands + inMagazine;

    state.ammoInMagazine = total >= stats.magSize ? stats.magSize : total;
    ammoHeld_[static_cast<size_t>(stats.ammoType)] =
        table_->HasFlag(item, WeaponFlag::AmmoClipLimit)
            ? 0
            : (ammoHeld_[static_cast<size_t>(stats.ammoType)] - state.ammoInMagazine) + inMagazine;

    if (item == kItemRocketLauncher) {
        // currentPlayerCreateRocket.
        if (state.rocketProp < 0 && state.ammoInMagazine > 0 && createRocket_) {
            createRocket_(hand);
        }
        return;
    }
    if (item == kItemShotgun || item == kItemAutoShotgun) {
        const int32_t left = AmmoInHandsWeapon(hand);
        state.visibleShells = left >= 5 ? 5 : left;
    }
}

void PlayerWeapons::TickNoise(float globalTimerDelta) {
    const int32_t right = WeaponId(HandRight);
    const int32_t left = WeaponId(HandLeft);
    const ItemStats& rightStats = table_->Item(right);
    const ItemStats& leftStats = table_->Item(left);

    if (right != kItemUnarmed && Firing(HandRight)) {
        hands_[HandRight].noise += rightStats.noiseIncreasePerShot;
        if (rightStats.loudnessMax < hands_[HandRight].noise) {
            hands_[HandRight].noise = rightStats.loudnessMax;
        }
    }
    if (left != kItemUnarmed && Firing(HandLeft)) {
        hands_[HandLeft].noise += leftStats.noiseIncreasePerShot;
        if (leftStats.loudnessMax < hands_[HandLeft].noise) {
            hands_[HandLeft].noise = leftStats.loudnessMax;
        }
    }

    for (const Hand hand : { HandRight, HandLeft }) {
        const ItemStats& stats = hand == HandRight ? rightStats : leftStats;
        float& noise = hands_[hand].noise;
        float reduction = (stats.noiseIncreasePerShot * globalTimerDelta) / (stats.noiseDecayLinearTime * 60.0f);
        const float reductionMax =
            ((noise - stats.loudnessMin) * globalTimerDelta) / (stats.noiseDecayScaledTime * 60.0f);
        if (reduction < reductionMax) {
            reduction = reductionMax;
        }
        noise -= reduction;
        if (noise < stats.loudnessMin) {
            noise = stats.loudnessMin;
        }
    }
}

void PlayerWeapons::IncrementHeldTime(int32_t clockTimer) {
    int32_t weapon1 = WeaponId(HandRight);
    int32_t weapon2 = WeaponId(HandLeft);
    if (!table_->HasFlag(weapon1, WeaponFlag::UseHoldTime)) {
        return;
    }
    if (!table_->HasFlag(weapon2, WeaponFlag::UseHoldTime)) {
        weapon2 = kItemUnarmed;
    }

    int32_t leastUsedTime = 0x7fffffff;
    size_t leastUsedIndex = 0;
    size_t i = 0;
    for (; i < heldTimes_.size(); i++) {
        HeldTime& entry = heldTimes_[i];
        if (entry.totalTime >= 0) {
            if (weapon1 == entry.weapon1 && weapon2 == entry.weapon2) {
                entry.totalTime += clockTimer;
                break;
            }
            if (entry.totalTime < leastUsedTime) {
                leastUsedTime = entry.totalTime;
                leastUsedIndex = i;
            }
        } else {
            leastUsedIndex = i;
            i = heldTimes_.size();
            break;
        }
    }

    if (i == heldTimes_.size()) {
        heldTimes_[leastUsedIndex] = { weapon1, weapon2, clockTimer };
    }
}

void PlayerWeapons::TickGameplay(const GunTickInput& input, GunHooks* hooks) {
    std::array<bool, 2> trigger = { false, false };

    previousTriggerDown_ = triggerDown_;
    triggerDown_ = input.triggerOn;
    if (!triggerDown_ && previousTriggerDown_) {
        triggerReleased_ = true;
    }

    if (triggerDown_) {
        const int32_t right = WeaponId(HandRight);
        const int32_t left = WeaponId(HandLeft);
        const int32_t current = currentTriggerHand_;
        const int32_t other = 1 - current;
        zTriggerTimer_ += input.clockTimer;

        // On a long hold the lead hand fires and the other hand joins it if it
        // has ammo or was already firing.
        const auto holdBoth = [&]() {
            trigger[static_cast<size_t>(current)] = true;
            if (CanUseWeapon(static_cast<Hand>(other)) || hands_[other].holdTime != 0) {
                trigger[static_cast<size_t>(other)] = true;
            }
        };
        // On a fresh press the lead hand changes if the other can fire or this
        // one cannot, and only the lead hand fires.
        const auto takeTurns = [&]() {
            if (!previousTriggerDown_ &&
                (CanUseWeapon(static_cast<Hand>(other)) || !CanUseWeapon(static_cast<Hand>(current)))) {
                currentTriggerHand_ = other;
            }
            trigger[static_cast<size_t>(currentTriggerHand_)] = true;
            trigger[static_cast<size_t>(1 - currentTriggerHand_)] = false;
        };

        if (right != kItemUnarmed && left != kItemUnarmed) {
            const bool alternatingRight = table_->HasFlag(right, WeaponFlag::DualWieldAlternating);
            const bool alternatingLeft = table_->HasFlag(left, WeaponFlag::DualWieldAlternating);
            if (alternatingRight && alternatingLeft) {
                if (zTriggerTimer_ > kDualWieldSwapTicks) {
                    holdBoth();
                } else {
                    takeTurns();
                }
            } else if (alternatingRight || alternatingLeft) {
                if (zTriggerTimer_ > kDualWieldSingleSwapTicks) {
                    holdBoth();
                } else {
                    // Before the hold threshold the hand whose weapon alternates leads.
                    const auto lead = alternatingRight ? HandRight : HandLeft;
                    const auto trail = static_cast<Hand>(1 - lead);
                    if (CanUseWeapon(lead) || hands_[lead].holdTime != 0) {
                        currentTriggerHand_ = lead;
                    } else if (CanUseWeapon(trail) || hands_[trail].holdTime != 0) {
                        currentTriggerHand_ = trail;
                    } else {
                        currentTriggerHand_ = 1 - currentTriggerHand_;
                    }
                    trigger[static_cast<size_t>(currentTriggerHand_)] = true;
                    trigger[static_cast<size_t>(1 - currentTriggerHand_)] = false;
                }
            } else if (zTriggerTimer_ > kDualWieldSingleSwapTicks) {
                holdBoth();
            } else {
                takeTurns();
            }
        } else {
            if (WeaponId(static_cast<Hand>(currentTriggerHand_)) == kItemUnarmed &&
                WeaponId(static_cast<Hand>(1 - currentTriggerHand_)) != kItemUnarmed) {
                currentTriggerHand_ = 1 - currentTriggerHand_;
            }
            trigger[static_cast<size_t>(currentTriggerHand_)] = true;
            trigger[static_cast<size_t>(1 - currentTriggerHand_)] = false;
        }
    } else {
        zTriggerTimer_ = 0;
    }

    TickHandState(HandRight, trigger[0], input, hooks);
    TickHandState(HandLeft, trigger[1], input, hooks);
    LoadHandModel(HandRight);
    LoadHandModel(HandLeft);
    TickNoise(input.globalTimerDelta);
    IncrementHeldTime(input.clockTimer);
    TickMagnet(input.clockTimer, input.controlsLocked);
}

void PlayerWeapons::TickMagnet(int32_t clockTimer, bool controlsLocked) {
    // WATCH_SOUND_DURATION_TICKS: how long the magnet pulls.
    constexpr int32_t kMagnetTicks = 250;
    if (magnetAttractTime_ < 0) {
        return;
    }
    magnetAttractTime_ += clockTimer;
    HandState& hand = hands_[HandRight];
    if (magnetAttractTime_ < kMagnetTicks) {
        if (!hand.audioHandle.IsPlaying() && !controlsLocked) {
            Game::Sound::PlaySfx(AudioDriver::kSfxMagneticHum, hand.audioHandle.Pending());
        }
    } else {
        magnetAttractTime_ = -1;
        hand.audioHandle.StopIfPlaying();
    }
}

void PlayerWeapons::TickHandState(Hand handNum, bool triggerOn, const GunTickInput& input, GunHooks* hooks) {
    HandState& hand = hands_[handNum];
    const WeaponTable& table = *table_;
    const RandomFn& random = input.random;
    int32_t item = ItemInHandOrWatchMenu(handNum);
    const int32_t ammoType = table.AmmoType(item);

    // Enters `state` with its clock at zero.
    const auto enter = [&hand](int32_t state) {
        hand.actionState = state;
        hand.stateTime = 0;
        hand.stateUpdates = 0;
    };
    // Ends the current animation at idle.
    const auto finish = [&hand, &enter] { enter(GunState::Idle); };
    // WEAPON_1P_ANIM_TIME(field_890).
    const auto now = [&hand] { return static_cast<float>(hand.stateTime); };
    // gunSample1PTransform into the hand's transform; false once `keyframes`
    // has ended at `at`.
    const auto play = [&hand, handNum](const Keyframe* keyframes, float at) {
        if (SampleKeyframes(keyframes, at, handNum == HandLeft, &hand.transform)) {
            hand.hasTransform = true;
            return true;
        }
        return false;
    };

    hand.previousHoldTime = hand.holdTime;
    hand.holdTime = triggerOn ? 1 : 0;
    if (!triggerOn) {
        hand.triggerReleased = 1;
    }
    hand.firingStatus = 0;
    hand.flashVisible = false;
    if (input.clockTimer > 0) {
        hand.stateTime += input.clockTimer;
        hand.stateUpdates += 1;
    }
    hand.hasTransform = false;

    if (hand.actionState == GunState::Idle) {
        if (hand.holdTime != 0 && item != kItemUnarmed &&
            (table.HasFlag(item, WeaponFlag::Clicky) || hand.ammoInMagazine > 0)) {
            enter(GunState::TriggerPress);
            hand.triggerReleased = 0;
        } else if (hand.currentAnimation != 0) {
            enter(hand.currentAnimation);
        }
        hand.currentAnimation = 0;

        if (hand.actionState == GunState::Idle && hand.ammoInMagazine == 0 && ammoType != 0 && !input.controlsLocked) {
            if (AmmoInHandsWeapon(handNum) > 0) {
                enter(GunState::ReloadStart);
            } else if (triggerReleased_) {
                const auto otherHand = static_cast<Hand>(1 - handNum);
                HandState& other = hands_[otherHand];
                const int32_t otherItem = ItemInHandOrWatchMenu(otherHand);
                if (other.actionState == GunState::Idle && other.currentAnimation == 0 &&
                    (otherItem == kItemUnarmed || (other.ammoInMagazine == 0 && table.AmmoType(otherItem) != 0 &&
                                                   AmmoInHandsWeapon(otherHand) <= 0))) {
                    AutoAdvanceOnDepleteAllAmmo();
                    hand.stateUpdates = 0;
                    hand.stateTime = 0;
                    hand.actionState = hand.currentAnimation;
                    hand.currentAnimation = 0;
                    other.stateUpdates = 0;
                    other.stateTime = 0;
                    other.actionState = other.currentAnimation;
                    other.currentAnimation = 0;
                }
            }
        }
    }

    if (hand.actionState == GunState::TriggerPress) {
        switch (item) {
            case kItemRuger:
            case kItemGrenadeLauncher:
                if (hand.stateTime >= kRugerFireTicks) {
                    enter(GunState::Fire);
                }
                break;
            case kItemCamera:
                if (hand.stateUpdates == 0) {
                    if (hooks != nullptr) {
                        hooks->SetFadeColour(0, 0, 0, 1.0f);
                    }
                } else if (hand.stateTime > 0) {
                    if (hooks != nullptr) {
                        hooks->AdjustFade(8.0f, 0, 0, 0, 0.0f);
                    }
                    enter(GunState::Fire);
                }
                break;
            case kItemPp7:
            case kItemPp7Silenced:
            case kItemDd44:
            case kItemKlobb:
            case kItemKf7:
            case kItemZmg:
            case kItemD5k:
            case kItemD5kSilenced:
            case kItemPhantom:
            case kItemAr33:
            case kItemRcp90:
            case kItemShotgun:
            case kItemAutoShotgun:
            case kItemSniperRifle:
            case kItemGoldenGun:
            case kItemSilverPp7:
            case kItemGoldPp7:
            case kItemLaser:
            case kItemWatchLaser:
            case kItemRocketLauncher:
            case kItemTrigger:
            case kItemTankShells:
            case kItemFlarePistol:
            case kItemPitonGun:
            case kItemWatchMagnetAttract:
                enter(GunState::Fire);
                break;
            case kItemTimedMine:
            case kItemProximityMine:
            case kItemRemoteMine:
            case kItemBombCase:
            case kItemPlastique:
            case kItemBug:
            case kItemMicroCamera:
            case kItemGoldenEyeKey:
                enter(GunState::MinePlace);
                break;
            case kItemKnife:
                enter((random() & 1) == 0 ? GunState::KnifeSlash1Begin : GunState::KnifeSlash2Begin);
                break;
            case kItemGrenade:
                if (hand.triggerReleased != 0 || hand.stateTime >= kGrenadeHoldTicks) {
                    lastZTriggerTimer_ = hand.stateTime;
                    enter(GunState::GrenadeThrow);
                }
                break;
            case kItemFist:
                enter((random() & 1) == 0 ? GunState::Punch1Strike : GunState::Punch2Strike);
                break;
            case kItemThrowKnife:
                enter(GunState::ThrowKnifeDraw);
                break;
            case kItemTaser:
                if (!play(Keyframes::kTaserFire, now())) {
                    enter(GunState::Fire);
                }
                break;
            case kItemBungee:
            case kItemDoorDecoder:
            case kItemBombDefuser:
            case kItemLockExploder:
            case kItemDoorExploder:
            case kItemWeaponCase:
            case kItemSafeCrackerCase:
            case kItemKeyAnalyserCase:
            case kItemBugDetector:
            case kItemExplosiveFloppy:
            case kItemPolarizedGlasses:
            case kItemDarkGlasses:
            case kItemCreditCard:
            case kItemGasKeyRing:
            case kItemDataThief:
            case kItemWatchIdentifier:
            case kItemWatchCommunicator:
            case kItemWatchGeigerCounter:
            case kItemWatchMagnetRepel:
            case kItemDatTape:
            case kItemKeyCard:
            case kItemKeyYale:
            case kItemKeyBolt:
                enter(GunState::UseItem);
                break;
            default:
                enter(GunState::Idle);
                break;
        }
        hand.volley = 0;
    }

    if (hand.actionState == GunState::Fire) {
        if (ammoType == 0 || hand.ammoInMagazine > 0) {
            const int32_t rate = table.Item(item).automaticFiringRate;
            switch (item) {
                case kItemCamera:
                case kItemWatchMagnetAttract:
                    if (hand.stateUpdates == 0) {
                        hand.firingStatus = input.controlsLocked ? 0 : 1;
                    } else {
                        enter(GunState::Recoil1);
                    }
                    break;
                case kItemPp7:
                case kItemPp7Silenced:
                case kItemDd44:
                case kItemShotgun:
                case kItemAutoShotgun:
                case kItemSniperRifle:
                case kItemRuger:
                case kItemGoldenGun:
                case kItemSilverPp7:
                case kItemGoldPp7:
                case kItemLaser:
                case kItemWatchLaser:
                case kItemGrenadeLauncher:
                case kItemRocketLauncher:
                case kItemTrigger:
                case kItemTankShells:
                case kItemFlarePistol:
                case kItemPitonGun:
                    if (hand.stateUpdates == 0) {
                        hand.flashVisible = true;
                        hand.firingStatus = input.controlsLocked ? 0 : 1;
                        // sub_GAME_7F05E808: the slide starts to travel back.
                        hand.slideRecoiling = true;
                    } else {
                        enter(GunState::Recoil1);
                    }
                    break;
                case kItemKlobb:
                case kItemKf7:
                case kItemZmg:
                case kItemD5k:
                case kItemD5kSilenced:
                case kItemPhantom:
                case kItemAr33:
                case kItemRcp90:
                    if (hand.stateUpdates == 0 || hand.holdTime != 0 ||
                        (table.HasFlag(item, WeaponFlag::BurstFire) && !input.aiming && (hand.volley % 3) != 0)) {
                        if (rate == 0 || (hand.stateUpdates % rate) == 0) {
                            hand.flashVisible = true;
                            hand.firingStatus = input.controlsLocked ? 0 : 1;
                        }
                    } else {
                        enter(GunState::Recoil1);
                    }
                    break;
                case kItemKnife:
                    if (hand.stateUpdates == 0 || hand.holdTime != 0) {
                        hand.firingStatus = 0;
                        hand.flashVisible = hand.firingStatus != 0;
                    } else {
                        enter(GunState::Recoil1);
                    }
                    break;
                case kItemTaser:
                    if (hand.stateUpdates == 0 || hand.holdTime != 0) {
                        play(Keyframes::kTaserRaise, 0.0f);
                        hand.firingStatus = 0;
                        hand.hasTransform = true;
                        hand.flashVisible = hand.firingStatus != 0;
                        if (hand.stateUpdates == 0) {
                            hand.firingStatus = input.controlsLocked ? 0 : 1;
                        }
                    } else {
                        enter(GunState::Recoil1);
                    }
                    break;
                default:
                    break;
            }

            if (hand.firingStatus != 0) {
                // TODO(https://github.com/liz-baker/frigate/issues/40): the
                // rumble pak (joyRumblePakStart).
                hand.ammoInMagazine -= 1;
                hand.volley += 1;
            }

            if (hand.actionState == GunState::Fire) {
                // A gun with a sound trigger rate sounds at most that often,
                // and not in the tick the other hand's shot sounded. Without
                // one every shot sounds.
                const ItemStats& stats = table.Item(item);
                const int32_t globalTimer = hooks != nullptr ? hooks->GlobalTimer() : 0;
                bool sounds = false;
                if (stats.soundTriggerRate > 0) {
                    if (hands_[1 - handNum].soundLastTime != globalTimer && hand.soundReadyTime < globalTimer) {
                        hand.soundReadyTime = stats.soundTriggerRate + globalTimer;
                        sounds = true;
                    }
                } else if (hand.firingStatus != 0) {
                    sounds = true;
                }

                // The original skips the sounds of a paused multiplayer game;
                // the port has one player and no paused stage.
                if (sounds) {
                    // A shot cuts off the sounds of the last ones.
                    hand.audioHandle.StopIfPlaying();
                    hand.secondAudioHandle.StopIfPlaying();
                    if (stats.sound != 0) {
                        if (hand.audioHandle.IsNull()) {
                            Game::Sound::PlaySfx(static_cast<int16_t>(stats.sound), hand.audioHandle.Pending());
                        } else if (hand.secondAudioHandle.IsNull()) {
                            Game::Sound::PlaySfx(static_cast<int16_t>(stats.sound), hand.secondAudioHandle.Pending());
                        }
                        hand.soundLastTime = globalTimer;
                    }
                }
                if (item == kItemWatchLaser) {
                    constexpr std::array<AudioDriver::SfxId, 2> kWatchLaserSounds = { AudioDriver::kSfxRicoLaser2,
                                                                                      AudioDriver::kSfxRicoLaser3 };
                    Game::Sound::PlaySfx(kWatchLaserSounds[random() & 1]);
                }
            }
        } else if (hand.stateUpdates > 0) {
            enter(GunState::Recoil1);
        } else {
            enter(GunState::DryFire);
            Game::Sound::PlaySfx(AudioDriver::kSfxEmptyGunFire);
        }
    }

    if (hand.actionState == GunState::Recoil1) {
        if (item == kItemTaser) {
            if (!play(Keyframes::kTaserRaise, now())) {
                finish();
            }
        } else {
            const ItemStats& stats = table.Item(item);
            const int32_t kick = stats.recoilSpeed[0];
            const int32_t settle = stats.recoilSpeed[1];

            if ((hand.triggerReleased != 0 && hand.stateTime >= kick + settle) ||
                (stats.singleFiringRate >= 0 && hand.triggerReleased == 0 &&
                 hand.stateTime >= kick + settle + stats.singleFiringRate)) {
                finish();
            } else if (hand.triggerReleased != 0 && hand.holdTime != 0 && hand.stateTime >= stats.recoilSpeed[2] &&
                       stats.recoilSpeed[3] >= 0 && hand.stateTime + stats.recoilSpeed[3] < kick + settle &&
                       hand.stateTime + stats.recoilSpeed[3] >= stats.recoilSpeed[2]) {
                enter(GunState::Recoil2);
                hand.recoil2Ticks = stats.recoilSpeed[3];
            } else if (hand.stateTime < kick + settle) {
                const float back = stats.recoilBack;
                const float up = stats.recoilUp;

                if (hand.stateTime == 0) {
                    hand.recoilFrom = hand.recoilOffset;
                    hand.recoilFromAngle = hand.recoilAngle;
                }

                // The gun is thrown back along the line it aims on, and up.
                const float horizontal = handNum == HandRight ? table.Item(ItemInHandOrWatchMenu(HandRight)).posX
                                                              : -table.Item(ItemInHandOrWatchMenu(HandLeft)).posX;
                hand.recoilToAngle = kTau - ((up * kTau) / 360.0f);
                hand.recoilTo[0] = ((horizontal - hand.aim[0]) * back) / 1000.0f;
                hand.recoilTo[1] = 0.0f;
                hand.recoilTo[2] = ((stats.posZ - hand.aim[2]) * back) / 1000.0f;

                float fraction;
                if (hand.stateTime < kick) {
                    fraction = GameSinf((static_cast<float>(hand.stateTime) * kHalfPi) / static_cast<float>(kick));
                } else {
                    fraction =
                        (GameCosf((static_cast<float>(hand.stateTime - kick) * kPi) / static_cast<float>(settle)) *
                         0.5f) +
                        0.5f;
                }

                hand.recoilAngle = LerpAngle(hand.recoilFromAngle, hand.recoilToAngle, fraction);
                hand.hasTransform = true;
                for (size_t axis = 0; axis < 3; axis++) {
                    hand.recoilOffset[axis] =
                        ((hand.recoilTo[axis] - hand.recoilFrom[axis]) * fraction) + hand.recoilFrom[axis];
                }
                hand.transform = RotationX(hand.recoilAngle);
                hand.transform.m[3][0] = hand.recoilOffset[0];
                hand.transform.m[3][1] = hand.recoilOffset[1];
                hand.transform.m[3][2] = hand.recoilOffset[2];
            }
        }
    }

    if (hand.actionState == GunState::Recoil2) {
        if (hand.stateTime == 0) {
            hand.recoilFrom = hand.recoilOffset;
            hand.recoilFromAngle = hand.recoilAngle;
            hand.recoilToAngle = 0.0f;
            hand.recoilTo = { 0.0f, 0.0f, 0.0f };
        }

        if (hand.stateTime < hand.recoil2Ticks) {
            const float fraction = (GameCosf((static_cast<float>(hand.recoil2Ticks - hand.stateTime) * kHalfPi) /
                                             static_cast<float>(hand.recoil2Ticks)) *
                                    0.5f) +
                                   0.5f;
            hand.recoilAngle = LerpAngle(hand.recoilFromAngle, hand.recoilToAngle, fraction);
            hand.hasTransform = true;
            for (size_t axis = 0; axis < 3; axis++) {
                hand.recoilOffset[axis] =
                    ((hand.recoilTo[axis] - hand.recoilFrom[axis]) * fraction) + hand.recoilFrom[axis];
            }
            hand.transform = RotationX(hand.recoilAngle);
            hand.transform.m[3][0] = hand.recoilOffset[0];
            hand.transform.m[3][1] = hand.recoilOffset[1];
            hand.transform.m[3][2] = hand.recoilOffset[2];
        } else {
            finish();
        }
    }

    if (hand.actionState == GunState::DryFire) {
        if (hand.stateUpdates == 0) {
            hand.slideRecoiling = true;
        }
        if (hand.triggerReleased != 0 || hand.stateTime >= kDryFireTicks) {
            finish();
        }
    }

    if (hand.actionState == GunState::SwitchLower) {
        if (hand.stateUpdates == 0) {
            hand.swapWait = kSwapWaitTicks;
        }
        if (hand.stateTime >= kLowerTicks) {
            ammoHeld_[static_cast<size_t>(table.AmmoType(item))] += hand.ammoInMagazine;
            hand.ammoInMagazine = 0;
            ReleaseRocket(handNum);
            hand.actionState = GunState::SwitchSwap;
            fistModel_ = inventory_->HasItem(kItemSniperRifle) ? kItemSniperRifle : kItemFist;
        } else {
            SetChangeTransform(hand, (static_cast<float>(hand.stateTime) * kLn2) / static_cast<float>(kLowerTicks));
        }
    }

    if (hand.actionState == GunState::SwitchSwap || hand.actionState == GunState::SwitchHold) {
        if (hand.animationTrigger == 0 || hand.stateTime >= hand.swapWait) {
            if (hand.actionState == GunState::SwitchSwap) {
                const auto otherHand = static_cast<Hand>(1 - handNum);
                const HandState& other = hands_[otherHand];
                if (other.actionState != GunState::SwitchSwap && other.actionState != GunState::SwitchLower) {
                    if (other.currentAnimation != GunState::SwitchLower && other.actionState != GunState::WatchLower &&
                        other.actionState != GunState::WatchSwap && other.actionState != GunState::WatchRaise &&
                        other.currentAnimation != GunState::WatchLower) {
                        if (handNum == HandRight) {
                            if (!ItemAvailableForHand(hand.nextWeapon, WeaponId(HandLeft))) {
                                EquipWeapon(HandLeft, kItemUnarmed);
                            }
                        } else if (!ItemAvailableForHand(WeaponId(HandRight), hand.nextWeapon)) {
                            hand.nextWeapon = kItemUnarmed;
                        }
                    }
                }
                UnEquipWeapon(handNum, hand.nextWeapon);
                item = ItemInHandOrWatchMenu(handNum);
                hand.actionState = GunState::SwitchHold;
            } else if (HandWithoutItem(handNum)) {
                enter(GunState::SwitchRaise);
            }
        }
        if (hand.actionState == GunState::SwitchSwap || hand.actionState == GunState::SwitchHold) {
            SetChangeTransform(hand, kLn2);
        }
    }

    if (hand.actionState == GunState::SwitchRaise) {
        if (hand.stateUpdates == 0) {
            FillMagazine(handNum);
            triggerReleased_ = false;
            if (input.clockTimer > 0 && HandWithoutItem(handNum) && (hooks == nullptr || hooks->RaiseSoundsAllowed())) {
                if (const AudioDriver::SfxId pickup = PickupSound(item); pickup != AudioDriver::kSfxNothing) {
                    Game::Sound::PlaySfx(pickup);
                }
            }
        }
        if (hand.stateTime >= kRaiseTicks || !ItemHasModel(item) || !table.HasFlag(item, WeaponFlag::ShowFirstPerson) ||
            table.HasFlag(item, WeaponFlag::HideFirstPersonHand)) {
            finish();
        } else {
            SetChangeTransform(hand, (static_cast<float>(kRaiseTicks - hand.stateTime) * kLn2) /
                                         static_cast<float>(kRaiseTicks));
        }
    }

    if (hand.actionState == GunState::ReloadStart) {
        if ((hand.ammoInMagazine < table.Item(item).magSize || table.HasFlag(item, WeaponFlag::AmmoClipLimit)) &&
            AmmoInHandsWeapon(handNum) > 0) {
            hand.actionState = GunState::ReloadLower;
        } else {
            finish();
        }
    }

    if (hand.actionState == GunState::ReloadLower) {
        if (hand.stateTime >= kReloadLowerTicks || !hand.modelShown) {
            enter(GunState::ReloadSwap);
            hand.swapWait = kReloadSwapWait;
        } else {
            const float angle = (static_cast<float>(hand.stateTime) * kLn2) / kLowerDivisor;
            SetReloadTransform(hand, handNum, angle, kLowerDivisor, item);
        }
    }

    if (hand.actionState == GunState::ReloadSwap) {
        if (hand.stateUpdates == 0 && HandWithoutItem(handNum) && (hooks == nullptr || hooks->RaiseSoundsAllowed()) &&
            CocksOnReload(item)) {
            Game::Sound::PlaySfx(AudioDriver::kSfxGunRiflecock);
        }
        if (hand.stateTime >= hand.swapWait && hand.stateUpdates >= 2) {
            enter(GunState::ReloadRaise);
        } else {
            SetReloadTransform(hand, handNum, kLn2, 1.0f, item);
        }
    }

    if (hand.actionState == GunState::ReloadRaise) {
        if (hand.stateUpdates == 0) {
            FillMagazine(handNum);
            triggerReleased_ = false;
        }
        if (hand.stateTime >= kReloadRaiseTicks || !ItemHasModel(item) ||
            !table.HasFlag(item, WeaponFlag::ShowFirstPerson) || table.HasFlag(item, WeaponFlag::HideFirstPersonHand)) {
            finish();
        } else {
            const float angle = (static_cast<float>(kReloadRaiseTicks - hand.stateTime) * kLn2) / kRaiseDivisor;
            SetReloadTransform(hand, handNum, angle, kRaiseDivisor, item);
        }
    }

    // The watch states swap the hand to the watch menu's item and back the
    // same way, on the change states' timings.
    if (hand.actionState == GunState::WatchLower) {
        if (hand.stateTime >= kWatchLowerTicks || !hand.modelShown) {
            enter(GunState::WatchSwap);
        } else {
            SetChangeTransform(hand, (static_cast<float>(hand.stateTime) * kLn2) / kLowerDivisor);
        }
    }

    if (hand.actionState == GunState::WatchSwap) {
        if (hand.stateUpdates == 0 || !HandWithoutItem(handNum)) {
            // sub_GAME_7F05DA8C.
            PlaceItemInHand(handNum, hand.nextWeapon);
            hand.weaponNumWatchMenu = hand.nextWeapon;
            item = ItemInHandOrWatchMenu(handNum);
        }
        if (HandWithoutItem(handNum)) {
            enter(GunState::WatchRaise);
        } else {
            SetChangeTransform(hand, kLn2);
        }
    }

    if (hand.actionState == GunState::WatchRaise) {
        if (hand.stateUpdates == 0 && item < kItemBombCase) {
            FillMagazine(handNum);
            triggerReleased_ = false;
        }
        if (hand.stateTime >= kWatchRaiseTicks || !ItemHasModel(item) ||
            !table.HasFlag(item, WeaponFlag::ShowFirstPerson) || table.HasFlag(item, WeaponFlag::HideFirstPersonHand)) {
            finish();
        } else {
            SetChangeTransform(hand, (static_cast<float>(kWatchRaiseTicks - hand.stateTime) * kLn2) / kRaiseDivisor);
        }
    }

    if (hand.actionState >= GunState::KnifeSlash1Begin && hand.actionState <= GunState::KnifeSlash2Recover) {
        const bool first = hand.actionState <= GunState::KnifeSlash1Recover;

        if ((hand.actionState == GunState::KnifeSlash1Begin || hand.actionState == GunState::KnifeSlash2Begin) &&
            hand.stateTime >= kKnifeStrikeTicks) {
            constexpr std::array<AudioDriver::SfxId, 3> kKnifeSwishes = { AudioDriver::kSfxKnifeThrow1,
                                                                          AudioDriver::kSfxKnifeThrow2,
                                                                          AudioDriver::kSfxKnifeThrow3 };
            Game::Sound::PlaySfx(kKnifeSwishes[random() % 3U]);
            hand.actionState = first ? GunState::KnifeSlash1Strike : GunState::KnifeSlash2Strike;
        }

        if (hand.actionState != GunState::KnifeSlash1Recover && hand.actionState != GunState::KnifeSlash2Recover &&
            hand.stateTime >= kKnifeRecoverTicks) {
            hand.firingStatus = 1;
            hand.actionState = first ? GunState::KnifeSlash1Recover : GunState::KnifeSlash2Recover;
        }

        if (!play(first ? Keyframes::kKnifeSlash1 : Keyframes::kKnifeSlash2, now())) {
            finish();
        }
    }

    if (hand.actionState >= GunState::Punch1Strike && hand.actionState <= GunState::Punch2Recover) {
        const bool sniper = fistModel_ == kItemSniperRifle;
        const Keyframe* keyframes = nullptr;

        if (hand.actionState == GunState::Punch1Strike || hand.actionState == GunState::Punch1Recover) {
            keyframes = sniper ? Keyframes::kSniperMelee1 : Keyframes::kFistMelee1;
            if (hand.actionState != GunState::Punch1Recover && hand.stateTime >= kPunchRecoverTicks) {
                hand.firingStatus = 1;
                hand.actionState = GunState::Punch1Recover;
            }
        } else {
            keyframes = sniper ? Keyframes::kSniperMelee2 : Keyframes::kFistMelee2;
            if (hand.actionState != GunState::Punch2Recover && hand.stateTime >= kPunchRecoverTicks) {
                hand.firingStatus = 1;
                hand.actionState = GunState::Punch2Recover;
            }
        }

        if (!play(keyframes, now())) {
            finish();
        }
    }

    if (hand.actionState == GunState::GrenadeThrow) {
        if (hand.ammoInMagazine > 0) {
            if (!play(Keyframes::kGrenadeThrow, now())) {
                hand.switchesOn = false;
                hand.firingStatus = 1;
                hand.ammoInMagazine -= 1;
                enter(GunState::GrenadeRecover);
            }
        } else {
            finish();
        }
    }

    if (hand.actionState == GunState::GrenadeRecover) {
        if (!play(Keyframes::kTimedMineThrow, now())) {
            hand.switchesOn = true;
            finish();
        }
    }

    if (hand.actionState == GunState::ThrowKnifeDraw) {
        if (hand.ammoInMagazine > 0) {
            if (hand.triggerReleased != 0) {
                hand.actionState = GunState::ThrowKnifeThrow;
            } else if (!play(Keyframes::kThrowKnifeDrawBack, now()) && !play(Keyframes::kThrowKnifeRelease, 0.0f)) {
                hand.actionState = GunState::ThrowKnifeThrow;
            }
        } else {
            finish();
        }
    }

    if (hand.actionState == GunState::ThrowKnifeThrow) {
        if (hand.ammoInMagazine > 0) {
            if (!play(Keyframes::kThrowKnifeDrawBack, now())) {
                hand.switchesOn = false;
                hand.firingStatus = 1;
                hand.ammoInMagazine -= 1;
                enter(GunState::ThrowKnifeRecover);
            }
        } else {
            finish();
        }
    }

    if (hand.actionState == GunState::ThrowKnifeRecover) {
        if (!play(Keyframes::kThrowKnifeRelease, now())) {
            hand.switchesOn = true;
            finish();
        }
    }

    if (hand.actionState == GunState::MinePlace) {
        if (hand.ammoInMagazine > 0 || table.HasFlag(item, WeaponFlag::Clicky)) {
            if (!play(Keyframes::kProxMineThrow, now())) {
                hand.switchesOn = false;
                hand.firingStatus = 1;
                hand.ammoInMagazine -= 1;
                enter(GunState::MineRecover);
            }
        } else {
            finish();
        }
    }

    if (hand.actionState == GunState::MineRecover) {
        if (!play(Keyframes::kRemoteMineThrow, now())) {
            hand.switchesOn = true;
            finish();
        }
    }

    if (hand.actionState == GunState::UseItem) {
        if (hand.stateUpdates == 0 && hooks != nullptr) {
            switch (item) {
                case kItemKeyAnalyserCase:
                    hooks->AnalyzeGoldenEyeKey();
                    break;
                case kItemWeaponCase:
                    hooks->GiveWeaponCaseItems();
                    break;
                case kItemBombDefuser:
                case kItemDataThief:
                case kItemDoorDecoder:
                case kItemExplosiveFloppy:
                case kItemDatTape:
                    hooks->ItemUsedOnProp();
                    break;
                default:
                    break;
            }
        }

        if (hand.triggerReleased != 0) {
            finish();
        }
    }
}

} // namespace Frigate::Level
