#pragma once

#include <vector>

#include <array>
#include <memory>
#include <string>

#include "gfx/FixedMtx.h"
#include "gfx/Gu.h"
#include "level/BgLevel.h"
#include "level/BgView.h"
#include "level/BgVisibility.h"
#include "level/Chrs.h"
#include "level/Fog.h"
#include "level/Frustum.h"
#include "level/LevelInfo.h"
#include "level/Objects.h"
#include "level/PropRender.h"
#include "libultraship/libultra/gbi.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// What bgLevelRender reads off the current player. Every pointer is drawn
// from by address, so it has to stay valid until the frame's lists have run.
struct RenderView {
    PlayerView player;
    // g_CurrentPlayer->curRoomIndex.
    int currentRoom = -1;
    // g_CurrentPlayer->current_model_pos: the origin the rooms are drawn
    // relative to.
    Vec3 modelPos{};
    // currentPlayerGetProjectionMatrix(), the projection props draw under.
    const Mtx::FixedMtx* propProjection = nullptr;
    // get_BONDdata_field_10E0(): projection and view combined, which rooms
    // draw under.
    const Mtx::FixedMtx* worldProjection = nullptr;
    // g_viProjectionMatrix.
    const Mtx::FixedMtx* viProjection = nullptr;
    // g_CurrentPlayer->field_5C, the view matrix.
    const Mtx::FixedMtx* modelview = nullptr;
    // The same matrix, in float form: what a prop's own matrix combines with
    // to draw under propProjection (a pure projection, unlike worldProjection).
    const Gu::MatrixF* modelviewF = nullptr;
    // sub_GAME_7F078474(): the look-at for reflection-mapped materials.
    const LookAt* lookAt = nullptr;
    // sub_GAME_7F0BD8F0(): the level's draw switch.
    bool drawEnabled = true;
    // bossGetStageNum(): chrpropsRenderPass's Cuba special case.
    LevelId stage = LevelId::Max;
    // getPlayer_c_lodscalez.
    float lodScaleZ = 1.0f;
};

// Puts the visible rooms' display lists into a frame (bgLevelRender and the
// per-room draws it drives).
class BgRender {
  public:
    BgRender(BgLevel& level, BgVisibility& visibility, Objects& objects, Doors& doors, Chrs& chrs, const Fog& fog);

    // initializeRoomData: call once the level has loaded.
    void Reset();
    // updateRoomStatusFlags: once per tick.
    void Tick();

    // chraiUpdateOnscreenPropCount: called once per frame, before Render.
    // `camera` is the player's; `modelPos` and `modelviewF` are what props draw
    // under this frame.
    void UpdateOnScreenProps(const PropCamera& camera, const Vec3& modelPos, const Gu::MatrixF& modelviewF);

    // chrlvActionTick for each character propsTick reaches.
    void SetChrActionTick(std::function<void(Chr&)> tick) {
        propRender_.SetChrActionTick(std::move(tick));
    }
    // What a character's tick does to its fighting.
    void SetChrCombat(ChrDraw::CombatHooks hooks) {
        propRender_.SetChrCombat(std::move(hooks));
    }
    // objTick for the vehicles, aircraft and cameras propsTick reaches.
    void SetObjectTick(std::function<void(PlacedObject&)> tick) {
        propRender_.SetObjectTick(std::move(tick));
    }
    void SetMonitorTick(std::function<void(PlacedObject&, uint32_t screens)> tick) {
        propRender_.SetMonitorTick(std::move(tick));
    }
    // The tank the player drives and its pose.
    void SetTank(std::function<int()> driven, std::function<void(PlacedObject&)> pose) {
        propRender_.SetTank(std::move(driven), std::move(pose));
    }
    // The flight of thrown and fired objects and the fuses of weapons.
    void SetProjectiles(Projectiles* projectiles) {
        propRender_.SetProjectiles(projectiles);
    }
    // The explosions, smoke and bullet marks on props.
    void SetPropEffects(PropEffects effects) {
        propRender_.SetEffects(std::move(effects));
    }
    // explosionRenderScorchBuffer and explosionCallRenderBulletImpactOnProp:
    // the pass that draws the scorches and bullet marks on the rooms.
    void SetRoomMarks(std::function<Gfx*(Gfx*)> draw) {
        roomMarks_ = std::move(draw);
    }
    // g_OnScreenPropList, farthest first.
    const std::vector<int>& OnScreenProps() const {
        return propRender_.OnScreen();
    }

    // bgLevelRender. `resourceManager` loads the props' models
    // (chrpropsRenderPass); a level with no rooms draws none.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const RenderView& view);

  private:
    // sub_GAME_7F0B3C8C: the primary pass, near rooms first, then the
    // secondary pass back to front.
    Gfx* RenderRooms(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const RenderView& view);
    Gfx* RenderRoomPrimary(Gfx* dl, int room, const RenderView& view);
    Gfx* RenderRoomSecondary(Gfx* dl, int room, const RenderView& view);
    // applyRoomMatrixToDisplayList.
    Gfx* ApplyRoomMatrix(Gfx* dl, int room, const RenderView& view);
    // Calls a room's display list; a room without one draws nothing.
    Gfx* CallRoomList(Gfx* dl, const RoomBlock& list);

    BgLevel& level_;
    BgVisibility& visibility_;
    const Fog& fog_;
    RoomSlots slots_;
    std::array<Mtx::FixedMtx, RoomSlots::kSlotCount> roomMatrices_{};
    // GlobalLight; drawn from by address.
    Lights1 globalLight_ = gdSPDefLights1(150, 150, 150, 255, 255, 255, 77, 77, 46);
    PropRender propRender_;
    std::function<Gfx*(Gfx*)> roomMarks_;
};

} // namespace Frigate::Level
