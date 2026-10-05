#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "gfx/Gu.h"
#include "level/Beams.h"
#include "level/BulletImpacts.h"
#include "level/Casings.h"
#include "level/MonitorScreen.h"
#include "level/PlayerWeapons.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/Random.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

struct GunFrame;

// What the firing hands start: the projectiles of gun.c.
class ProjectileLaunch {
  public:
    virtual ~ProjectileLaunch() = default;
    // gunUpdateAttachedRocket: the rocket the launcher carries is placed at its
    // muzzle.
    virtual void UpdateAttachedRocket(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) = 0;
    // The item a firing hand launches or throws.
    virtual void Fire(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) = 0;
};

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
    // currentPlayerGetViewToWorldMtxf.
    Mtxf viewToWorld = IdentityMtxf();
    // camGetWorldToScreenMtxf.
    Mtxf worldToView = IdentityMtxf();
    // bondviewGetPlayerStanHeight: the floor the player stands on.
    float floorHeight = 0.0f;
    // g_Casings: where the spent cartridges go.
    Casings* casings = nullptr;
    // getCurrentPlayerPrevPos: where the player was last tick.
    Vec3 previousPosition{};
    // What a firing hand throws or launches.
    ProjectileLaunch* launcher = nullptr;
    // The tank the player drives, when its prop is on screen
    // (get_ptr_for_players_tank with PROPFLAG_ONSCREEN): the direction its gun
    // points and the world position of the muzzle.
    bool tankAimed = false;
    Vec3 tankAim{};
    Vec3 tankMuzzle{};
    // sub_GAME_7F078474: the look-at for reflection-mapped materials.
    const LookAt* lookAt = nullptr;
    // randomGetNext.
    PlayerWeapons::RandomFn random = Frigate::RandomGetNext;
};

// bondviewSelectCuff: shows the cuff of the sleeve the Bond of `bondType`
// wears, among the six switch nodes that start at `firstSlot`.
void SelectCuff(Model::ModelInstance& model, int32_t bondType, size_t firstSlot);

// The player's first-person guns: posing each hand's model under the sway and
// the weapon-change animations (gunUpdateAndFire), and drawing them
// (gunRenderFirstPersonGunModels).
class GunDraw {
  public:
    // Starts a frame: the draws of the frame before last can go.
    void BeginFrame();

    // write_monitor_ani_control_blocks: the screens of the rocket launcher's
    // sight and the taser start their lists. `imageSize` gives the size of an
    // entry of the global image table and `selectImage` the commands that pick
    // one (SelectEffectImage).
    using SelectImageFn = std::function<Gfx*(Gfx* dl, int offset, int mode, int zbufferMode, bool intensity)>;
    void AttachMonitors(std::shared_ptr<const MonitorLists> lists, std::function<EffectImageSize(int offset)> imageSize,
                        SelectImageFn selectImage);

    // Where the bullet beams are drawn.
    void SetBeamDraw(BeamDraw* beams) {
        beams_ = beams;
    }

    // gunUpdateAndFireBothHands, up to the point the hands start firing.
    void Update(PlayerWeapons& weapons, const GunFrame& frame);

    // gunRenderCasings.
    Gfx* RenderCasings(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const Casings& casings,
                       const Gu::MatrixF& worldToScreen, uint32_t envColour, ModelFactory& models);

    // gunRenderFirstPersonGunModels.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, PlayerWeapons& weapons,
                const GunFrame& frame);

  private:
    // gunUpdateAndFire.
    void UpdateHand(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);
    // gunCreateBeamForHand.
    void CreateBeam(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) const;
    // sub_GAME_7F068508: the hand's shot ejects a casing.
    void EjectCasing(const PlayerWeapons& weapons, Hand hand, const GunFrame& frame) const;

    // The draws of the last two frames: their commands point at the matrices
    // and lists each owns, which the interpreter reads after the frame's list
    // is built.
    std::array<std::deque<Model::JointDraw>, 2> draws_;
    // The casing models, by path.
    std::map<std::string, std::shared_ptr<Model::ModelInstance>> casingModels_;
    size_t generation_ = 0;
    BeamDraw* beams_ = nullptr;
    // g_UnknownAnimController and g_TaserAnimController: the screens the
    // launcher's sight and the taser's display show, shared by both hands.
    std::shared_ptr<const MonitorLists> monitorLists_;
    std::function<EffectImageSize(int)> imageSize_;
    SelectImageFn selectImage_;
    MonitorScreen launcherScreen_;
    MonitorScreen taserScreen_;
    // g_WeaponEnvmapLight: the light on the environment-mapped weapons; drawn
    // from by address.
    Lights1 envmapLight_ = gdSPDefLights1(0x96, 0x96, 0x96, 0xff, 0xff, 0xff, -78, 77, 46);
};

} // namespace Frigate::Level
