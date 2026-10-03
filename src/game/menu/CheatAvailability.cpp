#include "game/menu/CheatAvailability.h"

#include "game/menu/CheatIds.h"
#include "game/save/SaveProgress.h"

namespace Frigate::Game::Menu {

namespace {

// LEVEL_SOLO_SEQUENCE, the levels whose completion on a folder awards a cheat.
enum Level : int32_t {
    kDam,
    kFacility,
    kRunway,
    kSurface1,
    kBunker1,
    kSilo,
    kFrigate,
    kSurface2,
    kBunker2,
    kStatue,
    kArchives,
    kStreets,
    kDepot,
    kTrain,
    kJungle,
    kControl,
    kCaverns,
    kCradle,
    kAztec,
    kEgypt,
};

// The level whose reward `cheat` is, or -1 when it is not one.
int32_t RewardLevel(int32_t cheat) {
    switch (cheat) {
        case Cheat::kInvincibility:
            return kFacility;
        case Cheat::kAllGuns:
            return kEgypt;
        case Cheat::kNoRadarMp:
            return kFrigate;
        case Cheat::kFastAnimation:
            return kStatue;
        case Cheat::kInvisibility:
            return kArchives;
        case Cheat::kInfiniteAmmo:
            return kControl;
        case Cheat::kDkMode:
            return kRunway;
        case Cheat::kTinyBond:
            return kSurface2;
        case Cheat::kSlowAnimation:
            return kDepot;
        case Cheat::kPaintball:
            return kDam;
        case Cheat::kSilverPp7:
            return kTrain;
        case Cheat::kGoldPp7:
            return kCradle;
        case Cheat::kEnemyRockets:
            return kStreets;
        case Cheat::k2xRocketLauncher:
            return kBunker1;
        case Cheat::k2xGrenadeLauncher:
            return kSurface1;
        case Cheat::k2xRcp90:
            return kCaverns;
        case Cheat::k2xThrowingKnife:
            return kBunker2;
        case Cheat::k2xHuntingKnife:
            return kJungle;
        case Cheat::k2xLaser:
            return kAztec;
        case Cheat::kTurboMode:
            return kSilo;
        default:
            return -1;
    }
}

} // namespace

bool IsCheatAvailable(const Save::SaveProgress& progress, int32_t folder, int32_t cheat) {
    if (cheat < Cheat::kExtraMpChars || cheat >= Cheat::kInvalid) {
        return false;
    }

    const int32_t level = RewardLevel(cheat);
    if (level >= 0) {
        return progress.IsCheatUnlocked(folder, level);
    }

    switch (cheat) {
        case Cheat::kMagnum:
            return progress.IsCradleCompleted(folder);
        case Cheat::kLaser:
            return progress.IsAztecCompletedOnSecretOr00(folder);
        case Cheat::kGoldenGun:
            return progress.IsEgyptCompletedOn00(folder);
        default:
            // The cheats the front-end never offers (the debug and
            // multiplayer-only ids) and the kUnlock save entries.
            return false;
    }
}

} // namespace Frigate::Game::Menu
