#pragma once

#include <array>
#include <memory>
#include <string>

#include "gfx/FixedMtx.h"
#include "gfx/Gu.h"
#include "level/BgLevel.h"
#include "level/BgView.h"
#include "level/BgVisibility.h"
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
};

// Puts the visible rooms' display lists into a frame (bgLevelRender and the
// per-room draws it drives).
class BgRender {
  public:
    BgRender(BgLevel& level, BgVisibility& visibility, Objects& objects, Doors& doors);

    // initializeRoomData: call once the level has loaded.
    void Reset();
    // updateRoomStatusFlags: once per tick.
    void Tick();

    // chraiUpdateOnscreenPropCount: called once per frame, before Render.
    // `frustum` and `viewerPosition` are the camera's own; `modelPos` and
    // `modelviewF` are what props draw under this frame.
    void UpdateOnScreenProps(const FrustumPlanes& frustum, const Vec3& viewerPosition, const Vec3& modelPos,
                             const Gu::MatrixF& modelviewF, float visibilityScale);

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
    Gfx* CallRoomList(Gfx* dl, const std::string& path);

    BgLevel& level_;
    BgVisibility& visibility_;
    RoomSlots slots_;
    std::array<Mtx::FixedMtx, RoomSlots::kSlotCount> roomMatrices_{};
    // GlobalLight; drawn from by address.
    Lights1 globalLight_ = gdSPDefLights1(150, 150, 150, 255, 255, 255, 77, 77, 46);
    PropRender propRender_;
};

} // namespace Frigate::Level
