#include "level/PlayerWeapons.h"

#include <cmath>

#include "game/Sound.h"
#include "level/ModelHandle.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
// randomGetNext() as a fraction of one (RANDOMFRAC).
constexpr float kRandomFraction = 2.3283064e-10f;
// MAX_AIMLOCK_SPEED_DEFAULT.
constexpr float kMaxAimLockSpeed = 0.86f;

float RandomFraction(const PlayerWeapons::RandomFn& random) {
    return static_cast<float>(random()) * kRandomFraction;
}

} // namespace

PlayerWeapons::~PlayerWeapons() {
    StopSounds();
}

void PlayerWeapons::StopSounds() {
    for (HandState& hand : hands_) {
        hand.audioHandle.StopIfPlaying();
    }
}

void PlayerWeapons::Init(std::shared_ptr<const WeaponTable> table, const Inventory* inventory, const RandomFn& random) {
    StopSounds();
    ModelFactory* models = models_;
    *this = PlayerWeapons{};
    models_ = models;
    table_ = std::move(table);
    inventory_ = inventory;
    // init_player_BONDdata_stats: the sniper rifle and the camera start at
    // their own zoom, not the full field of view.
    sniperZoom_ = table_->Item(kItemSniperRifle).zoom;
    cameraZoom_ = table_->Item(kItemCamera).zoom;
    // The hands' first blends: bgunCalculateBlend three times for each.
    for (int i = 0; i < 3; i++) {
        CalculateBlend(HandRight, random);
    }
    for (int i = 0; i < 3; i++) {
        CalculateBlend(HandLeft, random);
    }
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

void PlayerWeapons::SetMaxAmmo() {
    for (size_t ammoType = 0; ammoType < table_->AmmoTypeCount(); ammoType++) {
        GiveAmmo(static_cast<int32_t>(ammoType), table_->MaxAmmo(static_cast<int32_t>(ammoType)));
    }
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
    if (EquipAllGuns() && right < kItemBombCase && right == left && playerCount_ == 1 &&
        table_->HasFlag(right, WeaponFlag::CanDualWield)) {
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
            const int32_t item = pendingItem_[hand];
            // get_ptr_item_text_call_line and get_ptr_weapon_model_header_line
            // both take the fist to be the model cur_item_weapon_getname names.
            const ItemStats& stats = table_->Item(item == kItemFist ? fistModel_ : item);
            if (item >= 0 && !stats.model.empty() && stats.hasHeader && models_ != nullptr) {
                handModel_[hand] = models_->CreateGun(stats.model);
            }
            handInvisible_[hand] = 1;
            handItem_[hand] = item;
            pendingItem_[hand] = -1;
        } else {
            handInvisible_[hand]--;
        }
    }
}

void PlayerWeapons::DetermineEquippedItem() {
    const int32_t current = WeaponId(HandRight);
    equipCurItem_ = kItemUnarmed;
    const auto listed = [this](const InvItem& item) { return propListed_ && propListed_(item); };
    const int32_t count = inventory_->ListedCount(listed);
    for (int32_t i = 0; i < count; i++) {
        if (inventory_->ListedWeapon(i, listed) == current) {
            equipCurItem_ = i;
            return;
        }
    }
}

void PlayerWeapons::EquipPrevious() {
    EquipWeapon(HandRight, hands_[HandRight].previousWeapon);
    EquipWeapon(HandLeft, hands_[HandLeft].previousWeapon);
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
    ReleaseCharacterWeapon(hand);
    ReleaseRocket(hand);
    state.weaponNum = weapon;
    state.ammoInMagazine = 0;
    state.soundReadyTime = 0;
    state.soundLastTime = 0;
    DetermineEquippedItem();
}

void PlayerWeapons::DrawItemInHand(Hand hand, int32_t item) {
    HandState& state = hands_[hand];
    state.currentAnimation = GunState::WatchLower;
    state.nextWeapon = item;
}

void PlayerWeapons::RestoreFromWatchMenu(Hand hand) {
    HandState& state = hands_[hand];
    if (state.weaponNumWatchMenu >= 0) {
        PlaceItemInHand(hand, state.weaponNum);
        state.weaponNumWatchMenu = -1;
    }
}

void PlayerWeapons::SetWatchMenuItem(Hand hand, int32_t item) {
    PlaceItemInHand(hand, item);
    hands_[hand].weaponNumWatchMenu = item;
}

int32_t PlayerWeapons::AmmoInHandsWeapon(Hand hand) const {
    const int32_t weapon = WeaponId(hand);
    const int32_t count = ammoHeld_[static_cast<size_t>(table_->AmmoType(weapon))];
    if (weapon == kItemShotgun || weapon == kItemAutoShotgun) {
        const int32_t other = WeaponId(static_cast<Hand>(1 - hand));
        if (other == kItemShotgun || other == kItemAutoShotgun) {
            return count - hands_[1 - hand].visibleShells;
        }
    }
    return count;
}

void PlayerWeapons::TickShading(const ShadeColour& floor) {
    if (resetShadeColour_) {
        tileShade_ = floor;
        resetShadeColour_ = false;
    } else {
        EaseShading(tileShade_, floor);
    }
}

void PlayerWeapons::CalculateBlend(Hand hand, const RandomFn& random) {
    HandState& state = hands_[hand];
    const float mult = table_->Item(WeaponId(hand)).sway;

    const int32_t target = (state.curBlendPos + 2) % 4;
    state.curBlendPos = (state.curBlendPos + 1) % 4;
    const auto slot = static_cast<size_t>(target);

    state.blendLook[slot][0] = (RandomFraction(random) - 0.5f) * 0.08f * mult;
    state.blendLook[slot][1] = (RandomFraction(random) - 0.5f) * 0.1f * mult;
    state.blendLook[slot][2] = -1.0f;

    state.blendUp[slot][0] = (RandomFraction(random) - 0.5f) * 0.1f * mult;
    state.blendUp[slot][1] = 1.0f;
    state.blendUp[slot][2] = (RandomFraction(random) - 0.5f) * 0.1f * mult;

    state.blendPos[slot][0] = (RandomFraction(random) * 0.75f) + 1.5f;
    state.blendPos[slot][1] = (2.0f + RandomFraction(random)) * state.blendScale1;
    state.blendPos[slot][2] = (RandomFraction(random) - 0.5f) * 2.5f;

    if (state.sideFlag < 0) {
        state.blendPos[slot][0] *= -1.0f;
        state.sideFlag = state.sideFlag == -2 ? 1 : -2;
    } else {
        state.sideFlag = state.sideFlag == 2 ? -1 : 2;
    }

    state.blendScale1 = -state.blendScale1;
}

void PlayerWeapons::SetBondWeaponSway(float breathing, float bondBreathing, float maxSpeed, float vertaSpeed,
                                      float thetaSpeed, int32_t clockTimer, float globalTimerDelta,
                                      const RandomFn& random) {
    float speedVerta = vertaSpeed;
    if (speedVerta < 0.0f) {
        speedVerta = -speedVerta;
    }

    if (maxSpeed > 0.8f) {
        gunPosAmplitude_ = 1.0f;
    } else if (maxSpeed > 0.1f) {
        const float tmp = 1.0f - GameCosf(((maxSpeed - 0.1f) * kTau) / 2.8f);
        gunPosAmplitude_ = 0.8f * tmp + 0.2f;
    } else {
        gunPosAmplitude_ = 0.1f;
    }

    if (gunPosAmplitude_ < bondBreathing * 0.3f) {
        gunPosAmplitude_ = bondBreathing * 0.3f;
    }
    if (gunPosAmplitude_ < 0.5f * speedVerta) {
        gunPosAmplitude_ = 0.5f * speedVerta;
    }

    for (int32_t i = 0; i < clockTimer; i++) {
        amplitudeSum_ = (amplitudeSum_ * 0.95f) + gunPosAmplitude_;
    }
    gunPosAmplitude_ = amplitudeSum_ * 0.050000012f;

    const float minBreathing = 0.016666668f * speedVerta;
    if (breathing < minBreathing) {
        breathing = minBreathing;
    }
    for (int32_t i = 0; i < clockTimer; i++) {
        breathingSum_ = (breathingSum_ * 0.95f) + breathing;
    }
    breathing = breathingSum_ * 0.050000012f;

    const float step = breathing * globalTimerDelta;
    float dampt[2];
    dampt[0] = hands_[HandRight].dampt + step;
    while (dampt[0] >= 1.0f) {
        CalculateBlend(HandRight, random);
        dampt[0] -= 1.0f;
        syncOffset_++;
    }

    syncCount_ += globalTimerDelta;
    if (syncCount_ > 60.0f) {
        syncCount_ = 0.0f;
        syncChange_ = (RandomFraction(random) - 0.5f) * 0.2f / 60.0f;
    }
    if (syncChange_ + step > 0.0f) {
        gunSync_ += syncChange_;
    }
    if (gunSync_ > 0.5f) {
        gunSync_ = 0.5f;
    } else if (gunSync_ < -0.5f) {
        gunSync_ = -0.5f;
    } else if (gunSync_ < 0.1f && gunSync_ > -0.1f) {
        gunSync_ = gunSync_ > 0.0f ? -0.1f : 0.1f;
    }

    dampt[1] = dampt[0] + static_cast<float>(syncOffset_) + gunSync_;
    while (dampt[1] >= 1.0f) {
        CalculateBlend(HandLeft, random);
        dampt[1] -= 1.0f;
        syncOffset_--;
    }

    for (int i = 0; i < 2; i++) {
        hands_[i].dampt = dampt[i];
        hands_[i].thetaDisplacement = -1.75f * thetaSpeed;
        hands_[i].vertaDisplacement = -2.0f * vertaSpeed;
    }
}

void PlayerWeapons::SetOffsetRelated(float verta) {
    hands_[HandRight].offset2[2] = (1.0f - GameCosf(verta)) * 5.0f;
    hands_[HandLeft].offset2[2] = (1.0f - GameCosf(verta)) * 5.0f;
}

void PlayerWeapons::AimCrosshair(float turnX, float turnY, float crosshairDamp, float aimDamp, int32_t clockTimer,
                                 const GunView& view) {
    if (crosshairDamp != aim_.crosshairDamp) {
        aim_.crosshairX = (aim_.crosshairX * (1.0f - aim_.crosshairDamp)) / (1.0f - crosshairDamp);
        aim_.crosshairY = (aim_.crosshairY * (1.0f - aim_.crosshairDamp)) / (1.0f - crosshairDamp);
        aim_.crosshairDamp = crosshairDamp;
    }
    if (aimDamp != aim_.gunAimDamp) {
        aim_.gunAzimuth = (aim_.gunAzimuth * (1.0f - aim_.gunAimDamp)) / (1.0f - aimDamp);
        aim_.gunTurning = (aim_.gunTurning * (1.0f - aim_.gunAimDamp)) / (1.0f - aimDamp);
        aim_.gunAimDamp = aimDamp;
    }

    for (int32_t i = 0; i < clockTimer; i++) {
        aim_.crosshairX = (aim_.crosshairX * crosshairDamp) + turnX;
        aim_.crosshairY = (aim_.crosshairY * crosshairDamp) + turnY;
    }

    aim_.crosshairScreenX =
        (aim_.crosshairX * (1.0f - crosshairDamp) * view.screenWidth * 0.5f) + (view.screenWidth * 0.5f);
    aim_.crosshairScreenY =
        (aim_.crosshairY * (1.0f - crosshairDamp) * view.screenHeight * 0.5f) + (view.screenHeight * 0.5f);

    if (aim_.crosshairScreenX < 3.0f) {
        aim_.crosshairScreenX = 3.0f;
    } else if ((view.screenWidth - 4.0f) < aim_.crosshairScreenX) {
        aim_.crosshairScreenX = view.screenWidth - 4.0f;
    }
    if (aim_.crosshairScreenY < 3.0f) {
        aim_.crosshairScreenY = 3.0f;
    } else if ((view.screenHeight - 4.0f) < aim_.crosshairScreenY) {
        aim_.crosshairScreenY = view.screenHeight - 4.0f;
    }

    aim_.crosshairScreenX += view.screenLeft;
    aim_.crosshairScreenY += view.screenTop;

    for (int32_t i = 0; i < clockTimer; i++) {
        aim_.gunAzimuth = (aim_.gunAzimuth * aimDamp) + turnX;
        aim_.gunTurning = (aim_.gunTurning * aimDamp) + turnY;
    }

    aim_.aimScreenX = (aim_.gunAzimuth * (1.0f - aimDamp) * view.screenWidth * 0.5f) + (view.screenWidth * 0.5f);
    aim_.aimScreenY = (aim_.gunTurning * (1.0f - aimDamp) * view.screenHeight * 0.5f) + (view.screenHeight * 0.5f);

    aim_.aimScreenX += view.screenLeft;
    aim_.aimScreenY += view.screenTop;

    // transformAndNormalizeByLength2Dto3D of the aim point, a thousand units out.
    const float y = ((view.screenHeight * 0.5f) - (aim_.aimScreenY - view.screenTop)) * view.scaleY;
    const float x = ((aim_.aimScreenX - view.screenLeft) - (view.screenWidth * 0.5f)) * view.scaleX;
    const float z = -1.0f;
    const float norm = 1000.0f / std::sqrt((x * x) + (y * y) + (z * z));
    const Vec3 target = { x * norm, y * norm, -1.0f * norm };

    // sub_GAME_7F067AB4.
    for (const Hand hand : { HandRight, HandLeft }) {
        HandState& state = hands_[hand];
        state.aim[0] = state.dualShift + target[0];
        state.aim[1] = target[1];
        state.aim[2] = target[2];
    }
}

void PlayerWeapons::AimSwivel(float turnX, float turnY, int32_t clockTimer, const GunView& view) {
    // sub_GAME_7F067F58: the aim-lock speed is the weapon's, or the default if
    // that is slower.
    float aimLockSpeed = table_->Item(WeaponId(HandRight)).aimLockSpeed;
    if (aimLockSpeed < kMaxAimLockSpeed) {
        aimLockSpeed = kMaxAimLockSpeed;
    }
    AimCrosshair(turnX, turnY, kMaxAimLockSpeed, aimLockSpeed, clockTimer, view);
}

void PlayerWeapons::AimManual(float turnX, float turnY, int32_t clockTimer, const GunView& view) {
    const ItemStats& stats = table_->Item(WeaponId(HandRight));
    AimCrosshair(turnX, turnY, stats.crosshairSpeed, stats.aimLockSpeed, clockTimer, view);
}

} // namespace Frigate::Level
