#include "game/Sound.h"
#include "level/PickupText.h"
#include "level/StageAi.h"
#include "level/StageGun.h"

namespace Frigate::Level {

void Stage::GunBridge::GuardsHeardShot(float noise) {
    // bondviewGetVisibleToGuardsFlag.
    if (stage_.ai_ != nullptr && stage_.aiBridge_->Player().visibleToGuards) {
        stage_.ai_->GuardsHeardSound(noise);
    }
}

void Stage::GunBridge::ItemUsedOnProp() {
    if (!stage_.setup_ || stage_.player_.PropId() < 0) {
        return;
    }
    const std::vector<int>& farFirst = stage_.render_.OnScreenProps();
    const std::vector<int> nearFirst(farFirst.rbegin(), farFirst.rend());
    const int prop = Interact::Find(stage_.MakeInteractScene(), stage_.MakeInteractPlayer(), nearFirst);
    if (prop < 0) {
        return;
    }
    if (PlacedObject* object = stage_.objects_.ForProp(prop)) {
        object->state |= ObjectState::Activated;
    }
}

void Stage::GunBridge::Rumble(float duration) {
    stage_.RumblePlayer(duration);
}

void Stage::GunBridge::AnalyzeGoldenEyeKey() {
    // GUN_STR_D8 and GUN_STR_D9: what the analyser says with and without the key.
    constexpr int32_t kAnalysing = 0xD8;
    constexpr int32_t kNoKey = 0xD9;
    // PROP_CHRGOLDENEYEKEY, the model of the key.
    constexpr int kKeyModel = 248;

    // bondinvHasGEKey: a kept weapon prop made from the key's model.
    bool hasKey = false;
    for (const InvItem& item : stage_.intro_.inventory.Items()) {
        if (!item.IsProp() || stage_.objects_.Props()[item.prop].type != PropType::Weapon) {
            continue;
        }
        const PlacedObject* object = stage_.objects_.ForProp(item.prop);
        if (object != nullptr && object->model == kKeyModel) {
            hasKey = true;
            break;
        }
    }

    if (hasKey) {
        stage_.hud_.BottomShow(stage_.lang_.Get(StringId(TextBank::Gun, kAnalysing)));
        stage_.copiedGoldenEye_ = true;
        Game::Sound::PlaySfx(AudioDriver::kSfxKeyAnalyser);
        stage_.weapons_.EquipWeapon(HandRight, kItemGoldenEyeKey);
        stage_.weapons_.EquipWeapon(HandLeft, kItemUnarmed);
    } else {
        stage_.hud_.BottomShow(stage_.lang_.Get(StringId(TextBank::Gun, kNoKey)));
        stage_.weapons_.EquipPrevious();
    }
}

void Stage::GunBridge::GiveWeaponCaseItems() {
    PlayerWeapons& weapons = stage_.weapons_;
    const WeaponTable& table = weapons.Table();
    Inventory& inventory = stage_.intro_.inventory;
    const auto message = [this](int32_t ammoType, int32_t amount) {
        stage_.hud_.BottomShow(AmmoCollectedText(stage_.lang_, ammoType, amount));
    };

    Objects::AddAmmoToInventory(AmmoKind::Knife, 2, table, &weapons, &inventory, message);
    Objects::AddAmmoToInventory(AmmoKind::Grenade, 2, table, &weapons, &inventory, message);
    inventory.AddItem(kItemSniperRifle);
    // set_sound_effect_for_weapontype_collection(ITEM_SNIPERRIFLE).
    Game::Sound::PlaySfx(AudioDriver::kSfxPickupGun);
    stage_.hud_.BottomShow(WeaponPickupText(stage_.lang_, table, kItemSniperRifle));
    const int32_t ammoType = table.AmmoType(kItemSniperRifle);
    weapons.GiveAmmo(ammoType, weapons.AmmoHeld(ammoType) + table.Item(kItemSniperRifle).magSize);
    inventory.RemoveItem(kItemWeaponCase);
    weapons.EquipWeapon(HandRight, kItemSniperRifle);
    weapons.EquipWeapon(HandLeft, kItemUnarmed);
}

int32_t Stage::GunBridge::GlobalTimer() const {
    return stage_.clock_.globalTimer;
}

bool Stage::GunBridge::RaiseSoundsAllowed() const {
    return stage_.cameraMode_ != CameraMode::Intro && !stage_.playerInvincible_;
}

void Stage::GunBridge::SetFadeColour(int red, int green, int blue, float frac) {
    stage_.colour_.SetColour(red, green, blue, frac);
}

void Stage::GunBridge::AdjustFade(float time, int red, int green, int blue, float frac) {
    stage_.colour_.Adjust(time, red, green, blue, frac);
}

} // namespace Frigate::Level
