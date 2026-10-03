#include "level/HitSounds.h"

#include "game/Sound.h"
#include "level/Inventory.h"
#include "level/TextureHits.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

using namespace AudioDriver;

// ricochet_sounds_small.
constexpr std::array<SfxId, 20> kSmallRicochets = {
    kSfxRico8AfdmA, kSfxRico8AfdmB, kSfxRico8AfdmC, kSfxRico8AfdmD, kSfxRico8AfdmA, kSfxRico8AfdmB, kSfxRico8AfdmC,
    kSfxRico8AfdmD, kSfxRico8AfdmA, kSfxRico8AfdmB, kSfxRico8AfdmC, kSfxRico8AfdmD, kSfxRico5A,     kSfxRico5B,
    kSfxRico5C,     kSfxRico5D,     kSfxRico6HbbaA, kSfxRico6HbbaB, kSfxRico6HbbaC, kSfxRico6HbbaD,
};

// punch_sounds and bullet_flesh_sounds.
constexpr std::array<SfxId, 3> kPunches = { kSfxPunch1, kSfxPunch2, kSfxPunch3 };
constexpr std::array<SfxId, 2> kFleshHits = { kSfxHitBulletFlesh, kSfxHitBulletFlesh };

// laser_ricochet_sounds.
constexpr std::array<SfxId, 2> kLaserRicochets = { kSfxRicoLaser2, kSfxRicoLaser3 };

// ricochet_sounds_large.
constexpr std::array<SfxId, 36> kLargeRicochets = {
    kSfxRico12GbuA, kSfxRico12GbuB, kSfxRico12GbuC, kSfxRico12GbuD, kSfxRico6TajA,  kSfxRico6TajB,
    kSfxRico6TajC,  kSfxRico6TajD,  kSfxRico6TajA,  kSfxRico6TajB,  kSfxRico6TajC,  kSfxRico6TajD,
    kSfxRico6TajA,  kSfxRico6TajB,  kSfxRico6TajC,  kSfxRico6TajD,  kSfxRico4A,     kSfxRico4B,
    kSfxRico4B,     kSfxRico4C,     kSfxRico4A,     kSfxRico4B,     kSfxRico4B,     kSfxRico4C,
    kSfxRico4A,     kSfxRico4B,     kSfxRico4B,     kSfxRico4C,     kSfxRico5A,     kSfxRico5B,
    kSfxRico5C,     kSfxRico5D,     kSfxRico6HbbaA, kSfxRico6HbbaB, kSfxRico6HbbaC, kSfxRico6HbbaD,
};

} // namespace

Game::Sound::Handle* HitSounds::FreeState() {
    for (Game::Sound::Handle& state : states_) {
        if (state.IsNull()) {
            return &state;
        }
    }
    return nullptr;
}

void HitSounds::PropHit(int32_t item, const Vec3& position, bool fleshy, int surface, const RandomFn& random) {
    const int32_t volume = Game::Sound::VolumeAtPositionDefault(position);
    const uint32_t first = random();
    const uint32_t second = random();

    switch (item) {
        case kItemRemoteMine:
        case kItemProximityMine:
        case kItemTimedMine:
        case kItemBombCase:
        case kItemBug:
        case kItemMicroCamera:
        case kItemPlastique:
        case kItemWatchLaser:
        case kItemWatchMagnetAttract:
            return;
        default:
            break;
    }

    if (Game::Sound::Handle* state = FreeState()) {
        SfxId sound;
        if (!fleshy) {
            sound = item == kItemLaser ? kSfxRicoLaser1 : kSmallRicochets[first % kSmallRicochets.size()];
        } else if (item == kItemKnife) {
            sound = kSfxHitBulletSnow;
        } else if (item == kItemFist) {
            sound = kPunches[first % kPunches.size()];
        } else {
            sound = kFleshHits[first % kFleshHits.size()];
        }
        Game::Sound::PlaySfx(sound, state->Pending());
        Game::Sound::SetVolume(state->Get(), volume);
    }

    if (Game::Sound::Handle* state = FreeState(); state != nullptr && surface >= 0) {
        const SurfaceImpacts& impacts = SurfaceImpactsFor(surface);
        if (impacts.soundCount > 0) {
            Game::Sound::PlaySfx(impacts.sounds[second % static_cast<uint32_t>(impacts.soundCount)], state->Pending());
        }
        Game::Sound::PostVolumeAtPositionDefault(state->Get(), position);
    }
}

void HitSounds::WorldHit(int32_t item, const Vec3& position, int surface, const RandomFn& random) {
    const uint32_t first = random();
    const uint32_t second = random();

    if (Game::Sound::Handle* state = FreeState()) {
        if (item != kItemWatchLaser) {
            if (item == kItemLaser) {
                Game::Sound::PlaySfx(kLaserRicochets[first % kLaserRicochets.size()], state->Pending());
            } else {
                Game::Sound::PlaySfx(kLargeRicochets[first % kLargeRicochets.size()], state->Pending());
            }
        }
        Game::Sound::PostVolumeAtPositionDefault(state->Get(), position);
    }

    if (Game::Sound::Handle* state = FreeState(); state != nullptr && surface >= 0) {
        const SurfaceImpacts& impacts = SurfaceImpactsFor(surface);
        if (impacts.soundCount > 0) {
            Game::Sound::PlaySfx(impacts.sounds[second % static_cast<uint32_t>(impacts.soundCount)], state->Pending());
            Game::Sound::PostVolumeAtPositionDefault(state->Get(), position);
        }
    }
}

void HitSounds::GlassHit(const Vec3& position) {
    if (Game::Sound::Handle* state = FreeState()) {
        Game::Sound::PlaySfx(kSfxHitBulletGlass, state->Pending());
        Game::Sound::PostVolumeAtPositionDefault(state->Get(), position);
    }
}

} // namespace Frigate::Level
