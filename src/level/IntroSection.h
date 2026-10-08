#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "level/BgHeader.h"
#include "level/Inventory.h"
#include "level/Stan.h"
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
    // The item records that apply, in file order, which a respawn adds to the
    // emptied inventory again.
    std::vector<IntroItem> items;
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
    // start_stan: the stan tile under the spawn, kNoTile if none was found.
    int stanTile = kNoTile;
    // stanHeight: the floor height under the spawn.
    float stanHeight = 0;
    // field_6C and field_70, the state of the stan-height smoothing.
    float stanHeightFilter = 0;
    float stanHeightRemainder = 0;
    // eyeheight, as bondviewPlayerBeginLife sets it.
    float eyeHeight = 0;

    // The spawn's position with the eye at `eyeHeight` above the floor.
    Vec3 EyePosition() const {
        return { position[0], stanHeight + eyeHeight, position[2] };
    }
};

// bondviewGetRandomSpawnPadIndex: which of the start pads (`pads`, their
// positions) player `playerNum` starts at in a game of two or more players. The
// search steps `cursor` (the player's field_29E0) round the pads, skipping any
// within 1000 units of another player, and picks a pad at random with `random`
// (randomGetNext) when every one is. `others` holds each player's position,
// empty until the player's prop exists, which a player is no obstacle without.
size_t ChooseStartPad(const std::vector<Vec3>& pads, int playerNum, const std::vector<std::optional<Vec3>>& others,
                      int* cursor, const std::function<uint32_t()>& random);

// The second half of bondviewLoadSetupIntroSection, which places the player
// at the start pad `pad` (an index into `intro.startPads`; a single-player
// stage always takes the first), followed by bondviewPlayerBeginLife.
// `perspectiveHeight` is player_perspective_height. `stan` is the stage's, with
// the setup's pad tiles resolved.
PlayerSpawn PlacePlayer(const StageSetup& setup, IntroSetup& intro, Stan& stan, float perspectiveHeight,
                        size_t pad = 0);

// mp_respawn_handler's placement: the player starts a new life at the start
// pad `pad` (an index into `intro.startPads`) facing `lookAngle` (radians), at
// the eye height bondviewPlayerBeginLife set for the first life.
PlayerSpawn PlaceRespawn(const StageSetup& setup, const IntroSetup& intro, Stan& stan, float perspectiveHeight,
                         size_t pad, float lookAngle);

// IntroSwirl::flags (SetupIntroSwirl::bitflags).
namespace SwirlFlag {
// The record starts a new path of points: the spline does not cross into it.
constexpr int32_t kPathStart = 1;
// The offset is turned by the player's heading.
constexpr int32_t kRelativeToHeading = 2;
// The camera looks at the player's body alone, not the view direction.
constexpr int32_t kLookAtBody = 4;
} // namespace SwirlFlag

// Where the swirl puts the camera and what it looks at.
struct SwirlCamera {
    Vec3 position{};
    Vec3 lookAt{};
};

// bondviewCalcIntroSwirlCamera: the camera `time` ticks into swirl record
// `index`. `bond` is the player's smoothed position (field_3C4), `heading` the
// horizontal look direction (theta_transform) and `view` the applied view
// direction.
SwirlCamera CalcIntroSwirlCamera(const std::vector<IntroSwirl>& swirls, size_t index, float time, const Vec3& bond,
                                 const Vec3& heading, const Vec3& view);

} // namespace Frigate::Level
