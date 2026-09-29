#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

#include "level/PlayerWeapons.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/Random.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// What the gun code reads off the current player each frame.
struct GunFrame {
    // g_ClockTimer and g_GlobalTimerDelta.
    int32_t clockTimer = 1;
    float globalTimerDelta = 1.0f;
    // ducking_height_offset.
    float duckingHeightOffset = 0.0f;
    GunView view;
    // The screen setting is widescreen or cinema, or the ratio is 16:9.
    bool wideScreen = false;
    // bondtype: the cuff, and so the sleeve, Bond wears.
    int32_t bondType = 0;
    // getPlayer_c_lodscalez.
    float lodScaleZ = 1.0f;
    // sub_GAME_7F078474: the look-at for reflection-mapped materials.
    const LookAt* lookAt = nullptr;
    // randomGetNext.
    PlayerWeapons::RandomFn random = Frigate::RandomGetNext;
};

// The player's first-person guns: posing each hand's model under the sway and
// the weapon-change animations (gunUpdateAndFire), and drawing them
// (gunRenderFirstPersonGunModels).
class GunDraw {
  public:
    // Starts a frame: the draws of the frame before last can go.
    void BeginFrame();

    // gunUpdateAndFireBothHands, up to the point the hands start firing.
    void Update(PlayerWeapons& weapons, const GunFrame& frame);

    // gunRenderFirstPersonGunModels.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, PlayerWeapons& weapons,
                const GunFrame& frame);

  private:
    // gunUpdateAndFire.
    void UpdateHand(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);

    // The draws of the last two frames: their commands point at the matrices
    // and lists each owns, which the interpreter reads after the frame's list
    // is built.
    std::array<std::deque<Model::JointDraw>, 2> draws_;
    size_t generation_ = 0;
    // g_WeaponEnvmapLight: the light on the environment-mapped weapons; drawn
    // from by address.
    Lights1 envmapLight_ = gdSPDefLights1(0x96, 0x96, 0x96, 0xff, 0xff, 0xff, -78, 77, 46);
};

} // namespace Frigate::Level
