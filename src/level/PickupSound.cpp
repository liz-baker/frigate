#include "level/PickupSound.h"

#include "game/Sound.h"
#include "level/PickupText.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

void PlayAmmoCollectionSound(int32_t ammoType) {
    using namespace AudioDriver;
    switch (ammoType) {
        case AmmoKind::Ammo9mm:
        case AmmoKind::Ammo9mm2:
        case AmmoKind::Rifle:
        case AmmoKind::Shotgun:
        case AmmoKind::Grenade:
        case AmmoKind::Rockets:
        case AmmoKind::GrenadeRound:
        case AmmoKind::Magnum:
        case AmmoKind::GoldenGun:
        case AmmoKind::Darts:
        case AmmoKind::ExplosivePen:
        case AmmoKind::Flare:
        case AmmoKind::Piton:
        case AmmoKind::Dynamite:
        case AmmoKind::GoldenEyeKey:
        case AmmoKind::Token:
            Game::Sound::PlaySfx(kSfxPickupAmmo);
            break;
        case AmmoKind::RemoteMine:
        case AmmoKind::ProximityMine:
        case AmmoKind::TimedMine:
        case AmmoKind::BombCase:
        case AmmoKind::Bug:
        case AmmoKind::MicroCamera:
        case AmmoKind::Plastique:
            Game::Sound::PlaySfx(kSfxPickupMine);
            break;
        case AmmoKind::Knife:
            Game::Sound::PlaySfx(kSfxPickupKnife);
            break;
        default:
            break;
    }
}

void PlayWeaponCollectionSound(int32_t item) {
    using namespace AudioDriver;
    if (item == kItemKnife || item == kItemThrowKnife) {
        Game::Sound::PlaySfx(kSfxPickupKnife);
    } else if (item == kItemRemoteMine || item == kItemProximityMine || item == kItemTimedMine ||
               item == kItemBombCase || item == kItemBug || item == kItemMicroCamera || item == kItemPlastique) {
        Game::Sound::PlaySfx(kSfxPickupMine);
    } else if (item == kItemGrenade || item == kItemGrenadeRound || item == kItemRocketRound) {
        Game::Sound::PlaySfx(kSfxPickupAmmo);
    } else if (item == kItemLaser) {
        Game::Sound::PlaySfx(kSfxPickupLaser);
    } else {
        Game::Sound::PlaySfx(kSfxPickupGun);
    }
}

} // namespace Frigate::Level
