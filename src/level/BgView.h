#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "level/BgVisibility.h"

namespace Frigate::Level {

// The video interface's view of the frame (viGetX, viGetY, viGetViewLeft,
// viGetViewTop, viGetViewWidth, viGetViewHeight).
struct ViewMetrics {
    int32_t x = 0;
    int32_t y = 0;
    int32_t viewLeft = 0;
    int32_t viewTop = 0;
    int32_t viewWidth = 0;
    int32_t viewHeight = 0;
};

// The current player's screen rectangle (viewleft, viewtop, viewx, viewy).
struct PlayerView {
    int32_t left = 0;
    int32_t top = 0;
    int32_t width = 0;
    int32_t height = 0;
    // A display wider than 4:3 shows `widen` times the view's width, centered:
    // room bounds are worked out in that wider screen space, whose left edge is
    // `wideLeft`, and map back onto the view's own columns for the scissor.
    float widen = 1.0f;
    float wideLeft = 0.0f;
};

// bgViewRelated: how far the visibility area is pulled in from the frame's
// edges.
constexpr std::array<int32_t, 4> kDefaultViewRelated = { 1, 1, -1, -1 };

// bgUpdateCurrentPlayerScreenMinMax: the player's on-screen area
// (screensize), the visibility pass's starting bounds.
ScreenRect ScreenMinMax(const ViewMetrics& vi, const std::array<int32_t, 4>& viewRelated = kDefaultViewRelated);

// A scissor box in pixels; `right`/`bottom` are exclusive edges.
struct Scissor {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

// bgScissorCurrentPlayerView: the box clipped to the player's view.
Scissor ClipScissor(const PlayerView& view, int32_t left, int32_t top, int32_t right, int32_t bottom);
// bgScissorCurrentPlayerViewDefault: the whole view.
Scissor ViewScissor(const PlayerView& view);
// bgScissorCurrentPlayerViewF: a room's float bounds; the far edges round up.
Scissor BoundsScissor(const PlayerView& view, const ScreenRect& bounds);

// The matrix slots rooms are drawn through (roomStatusFlags, roomIndices,
// roomOwners and each room's mtxid in bgroomtrans.c). A room keeps its slot,
// and the matrix built into it, while the player stays in the room that
// built it.
class RoomSlots {
  public:
    static constexpr int kSlotCount = 300;

    // initializeRoomData.
    void Reset(int roomCount);

    // setupRoomTransformationMatrix's slot bookkeeping: the slot to draw
    // `room` through. `rebuild` is set when the slot's matrix is stale.
    int Acquire(int room, int currentRoom, bool& rebuild);

    // updateRoomStatusFlags: run once a tick; slots unused for two ticks free up.
    void Tick();

  private:
    // findAvailableRoomIndex.
    int FindFree() const;
    void ResetSlot(int slot);

    std::array<uint8_t, kSlotCount> status_{};
    std::array<int32_t, kSlotCount> room_{};
    std::array<int32_t, kSlotCount> owner_{};
    std::vector<int32_t> roomSlot_;
};

// The model matrix a room is drawn with (setupRoomTransformationMatrix):
// `roomScale` is the stage's reciprocal scale (room_data_float2), and
// `modelPos` the current room's scaled position (current_model_pos).
void RoomTransform(const Vec3& roomPosition, float roomScale, const Vec3& modelPos, float (&out)[4][4]);

} // namespace Frigate::Level
