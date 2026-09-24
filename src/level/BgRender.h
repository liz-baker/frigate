#pragma once

#include <array>
#include <string>

#include "gfx/FixedMtx.h"
#include "level/BgLevel.h"
#include "level/BgView.h"
#include "level/BgVisibility.h"
#include "libultraship/libultra/gbi.h"

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
    // sub_GAME_7F078474(): the look-at for reflection-mapped materials.
    const LookAt* lookAt = nullptr;
    // sub_GAME_7F0BD8F0(): the level's draw switch.
    bool drawEnabled = true;
};

// Puts the visible rooms' display lists into a frame (bgLevelRender and the
// per-room draws it drives).
class BgRender {
  public:
    BgRender(BgLevel& level, BgVisibility& visibility);

    // initializeRoomData: call once the level has loaded.
    void Reset();
    // updateRoomStatusFlags: once per tick.
    void Tick();

    // bgLevelRender.
    Gfx* Render(Gfx* dl, const RenderView& view);

  private:
    // sub_GAME_7F0B3C8C: the primary pass, near rooms first, then the
    // secondary pass back to front.
    Gfx* RenderRooms(Gfx* dl, const RenderView& view);
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
};

} // namespace Frigate::Level
