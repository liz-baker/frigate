#include "level/IntroSection.h"

#include <cmath>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
// FIELD_6C_FACTOR, US.
constexpr float kStanHeightFilterFactor = 0.170000016689f;
// init_player_BONDdata's eyeheight: the height start_pos is worked out with,
// before bondviewPlayerBeginLife sets the real one.
constexpr float kInitialEyeHeight = 1.0f;
// The clearance sub_GAME_7F0AFB78 is given when there is no start pad.
constexpr float kNoStartPadRadius = 30.0f;

// bondviewYPositionRelated: the floor height under a world position on `tile`.
float FloorHeight(const Stan& stan, int tile, float x, float z, float fallback) {
    // TODO(https://github.com/liz-baker/frigate/issues/148): the tank prop's own
    // floor (g_PlayerTankProp) replaces this when the player drives one.
    // TODO(https://github.com/liz-baker/frigate/issues/107): field_2A6C/field_2A70,
    // the ladder tile the player is on, replace `tile` while set.
    // The game reads through the tile unchecked; a tile the pad or search did
    // not resolve takes the fallback.
    if (tile == kNoTile) {
        return fallback;
    }
    return stan.PositionY(tile, x, z);
}

} // namespace

IntroSetup LoadIntroSection(const StageSetup& setup, int32_t demoSlot, const std::function<uint32_t()>& random) {
    // TODO(https://github.com/liz-baker/frigate/issues/146): the KIA reset at
    // the top of bondviewLoadSetupIntroSection.
    // TODO(https://github.com/liz-baker/frigate/issues/153): its forced disarm.
    // TODO(https://github.com/liz-baker/frigate/issues/148): its tank reset.
    // TODO(https://github.com/liz-baker/frigate/issues/140): its camera fade,
    // cinema and mission timer state.
    // TODO(https://github.com/liz-baker/frigate/issues/136): Cuba's extra
    // frame buffer (`resolution`).
    IntroSetup intro;
    bool startingWeaponSet = false;

    for (const IntroRecord& record : setup.Intro()) {
        if (const auto* spawn = std::get_if<IntroSpawn>(&record)) {
            if (spawn->demoSlot == demoSlot) {
                intro.startPads.push_back(static_cast<size_t>(spawn->pad));
            }
        } else if (const auto* item = std::get_if<IntroItem>(&record)) {
            if (item->demoSlot == demoSlot) {
                // TODO(https://github.com/liz-baker/frigate/issues/153):
                // weaponLoadProjectileModels for both items.
                if (item->itemLeft >= 0) {
                    intro.inventory.AddDoubles(item->itemRight, item->itemLeft);
                } else {
                    intro.inventory.AddItem(item->itemRight);
                }
                if (!startingWeaponSet) {
                    intro.startingWeaponRight = item->itemRight;
                    startingWeaponSet = true;
                    if (item->itemLeft >= 0) {
                        intro.startingWeaponLeft = item->itemLeft;
                    }
                }
            }
        } else if (const auto* ammo = std::get_if<IntroAmmo>(&record)) {
            if (ammo->demoSlot == demoSlot) {
                intro.ammo.push_back(*ammo);
            }
        } else if (const auto* swirl = std::get_if<IntroSwirl>(&record)) {
            intro.swirls.push_back(*swirl);
        } else if (const auto* anim = std::get_if<IntroAnim>(&record)) {
            intro.animation = anim->animation;
        } else if (const auto* cuff = std::get_if<IntroCuff>(&record)) {
            intro.bondType = cuff->bondType;
        } else if (const auto* camera = std::get_if<IntroCamera>(&record)) {
            // TODO(https://github.com/liz-baker/frigate/issues/140): langGet on
            // the caption ids.
            intro.cameras.push_back(*camera);
        } else if (const auto* watch = std::get_if<IntroWatch>(&record)) {
            intro.watchTime = 0;
            if (watch->minutes > 0) {
                intro.watchTime += (watch->minutes % 60) * (60 * 60);
            }
            if (watch->hours > 0) {
                intro.watchTime += (watch->hours % 12) * (60 * 60 * 60);
            }
        } else if (const auto* credits = std::get_if<IntroCredits>(&record)) {
            intro.creditsTableOffset = credits->tableOffset;
        }
    }

    if (!intro.cameras.empty()) {
        // The game keeps the cameras as a chain from the last record back, and
        // walks `pick` links from it.
        const size_t pick = random() % intro.cameras.size();
        intro.selectedCamera = intro.cameras.size() - 1 - pick;
    }

    intro.inventory.AddItem(kItemFist);
    if (!startingWeaponSet) {
        intro.startingWeaponRight = kItemFist;
    }
    return intro;
}

PlayerSpawn PlacePlayer(const StageSetup& setup, IntroSetup& intro, Stan& stan, float perspectiveHeight) {
    // TODO(https://github.com/liz-baker/frigate/issues/107): the movement
    // state the game seeds here: field_78/7C/80, field_488 (change_player_pos_to_target
    // and its theta transform) and field_3B8.
    // TODO(https://github.com/liz-baker/frigate/issues/157): the player's prop
    // (chrpropAllocate, its position and stan, chrpropActivate/Enable).
    // TODO(https://github.com/liz-baker/frigate/issues/140): the camera mode
    // (bondviewSetCameraMode).
    // TODO(https://github.com/liz-baker/frigate/issues/146): the death
    // animation counts.
    PlayerSpawn spawn;
    float lookAngle = 0.0f;

    if (!intro.startPads.empty()) {
        // A single-player stage always takes the first start pad;
        // bondviewGetRandomSpawnPadIndex is for multiplayer.
        const Pad& pad = setup.Pads()[intro.startPads.front()];
        spawn.position[0] = pad.position[0];
        spawn.position[2] = pad.position[2];
        spawn.stanTile = pad.stanTile;
        spawn.stanHeight = FloorHeight(stan, spawn.stanTile, spawn.position[0], spawn.position[2], pad.position[1]);
        spawn.position[1] = kInitialEyeHeight + spawn.stanHeight;
        lookAngle = kTau - std::atan2(pad.look[0], pad.look[2]);
    } else {
        // A stage without start pads takes the walkable position nearest the
        // origin that a circle of radius 30 fits at.
        spawn.stanTile = stan.FindNearestWalkable(&spawn.position, kNoStartPadRadius);
        spawn.stanHeight = FloorHeight(stan, spawn.stanTile, spawn.position[0], spawn.position[2], 0.0f);
        spawn.position[1] = kInitialEyeHeight + spawn.stanHeight;
    }
    spawn.stanHeightRemainder = spawn.stanHeight;
    spawn.theta = (lookAngle * 360.0f) / kTau;
    spawn.stanHeightFilter = spawn.stanHeight / kStanHeightFilterFactor;
    spawn.look = { -std::sin(lookAngle), 0.0f, std::cos(lookAngle) };

    // bondviewPlayerBeginLife.
    spawn.eyeHeight = perspectiveHeight * 185.0f * 1.0f - 10.0f;
    intro.inventory.AddItem(kItemFist);
    // currentPlayerEquipWeaponWrapper and solo_char_load only run for two or
    // more players.
    return spawn;
}

} // namespace Frigate::Level
