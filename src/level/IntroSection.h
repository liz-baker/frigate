#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "level/BgHeader.h"
#include "level/Inventory.h"
#include "level/StageSetup.h"

namespace Frigate::Level {

// What bondviewLoadSetupIntroSection reads out of the setup's intro section
// for one player.
struct IntroSetup {
    // g_Startpad: indexes into the setup's pads.
    std::vector<size_t> startPads;
    // starting_weapon: what the player equips first.
    int32_t startingWeaponRight = kItemUnarmed;
    int32_t startingWeaponLeft = kItemUnarmed;
    Inventory inventory;
    // The ammo the intro grants, in file order.
    std::vector<IntroAmmo> ammo;
    // The swirl records; the first is g_IntroSwirl.
    std::vector<IntroSwirl> swirls;
    // g_IntroAnimationIndex.
    int32_t animation = 0;
    // The Bond type a cuff record sets.
    std::optional<int32_t> bondType;
    std::vector<IntroCamera> cameras;
    // ptr_random06cam_entry: the camera picked at random, an index into
    // `cameras`; empty when there are none.
    std::optional<size_t> selectedCamera;
    // watch_time_0, in ticks.
    int32_t watchTime = 0;
    // credits_pointer, as an offset into the setup file.
    std::optional<uint32_t> creditsTableOffset;
};

// bondviewLoadSetupIntroSection, up to where it places the player. `demoSlot`
// is check_ramrom_flags(): the demo being played, 0 outside a demo. `random`
// is randomGetNext.
IntroSetup LoadIntroSection(const StageSetup& setup, int32_t demoSlot, const std::function<uint32_t()>& random);

// Where the intro puts the player, and the per-life state
// bondviewPlayerBeginLife then sets.
struct PlayerSpawn {
    // start_pos.
    Vec3 position{};
    // vv_theta, in degrees, and the horizontal look direction it gives
    // (theta_transform).
    float theta = 0;
    Vec3 look{ 0.0f, 0.0f, 1.0f };
    // stanHeight: the floor height under the spawn.
    float stanHeight = 0;
    // field_6C and field_70, the state of the stan-height smoothing.
    float stanHeightFilter = 0;
    float stanHeightRemainder = 0;
    // eyeheight, as bondviewPlayerBeginLife sets it.
    float eyeHeight = 0;
    int killsThisLife = 0;
    int lifeStartTime60 = 0;
    int healthDisplayTime = 0;

    // The spawn's position with the eye at `eyeHeight` above the floor.
    Vec3 EyePosition() const {
        return { position[0], stanHeight + eyeHeight, position[2] };
    }
};

// The second half of bondviewLoadSetupIntroSection, which places the player
// at the first start pad, followed by bondviewPlayerBeginLife.
// `perspectiveHeight` is player_perspective_height.
PlayerSpawn PlacePlayer(const StageSetup& setup, IntroSetup& intro, float perspectiveHeight);

} // namespace Frigate::Level
