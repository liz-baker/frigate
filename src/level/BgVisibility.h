#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "level/BgLevel.h"

namespace Frigate::Level {

// A screen rectangle (bbox2d / rectbbox), in pixels.
struct ScreenRect {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
};

// Shrinks `a` to its overlap with `b`; false if nothing is left (bgRectIntersect).
bool RectIntersect(ScreenRect& a, const ScreenRect& b);
// Grows `a` to cover `b` too (bgRectOutersect).
void RectUnion(ScreenRect& a, const ScreenRect& b);
// Loosely checks that `outer` surrounds the top-left corner of `inner`
// (bgRectIsInside).
bool RectIsInside(const ScreenRect& inner, const ScreenRect& outer);

// What the visibility pass reads off the current player's camera.
struct VisibilityView {
    // camGetWorldToScreenMtxf(): row-vector, translation in row 3.
    std::array<std::array<float, 4>, 4> worldToScreen{};
    // bondviewGetCurrentPlayersPosition().
    Vec3 position{};
    // g_CurrentPlayer->screensize.
    // TODO(https://github.com/liz-baker/frigate/issues/105): bgUpdateCurrentPlayerScreenMinMax, which derives this
    // from the viewport.
    ScreenRect screen;
    // viGetZRange()[1].
    float zFar = 0;
    // The player's projection terms (c_screenleft, c_screentop,
    // c_halfwidth, c_halfheight, c_recipscalex, c_recipscaley).
    float screenLeft = 0;
    float screenTop = 0;
    float halfWidth = 0;
    float halfHeight = 0;
    float recipScaleX = 1;
    float recipScaleY = 1;
    // g_BgCurrentRoom.
    int currentRoom = 0;
};

// A room chosen for drawing (s_bound_info).
struct RoomDraw {
    int room = 0;
    // The deepest portal traversal that reached it.
    int depth = 0;
    // The screen area it may appear in.
    ScreenRect bounds;
    // Set when reached through a special portal or without portal traversal.
    int next = 0;
};

// Works out which rooms the camera can see: the per-frame half of bg.c's
// portal machinery. Each frame's result stays until the next
// DetermineVisibleRooms.
class BgVisibility {
  public:
    explicit BgVisibility(BgLevel& level);

    // bgDetermineVisibleRooms.
    void DetermineVisibleRooms(const VisibilityView& view);

    // The rooms to draw (dword_8007FFA0, g_BgNumberOfRoomsDrawn).
    const std::vector<RoomDraw>& RoomsDrawn() const {
        return drawn_;
    }
    // bgGet2dBboxByRoomId: the screen area a drawn room may appear in.
    bool GetRoomBounds2d(int room, ScreenRect& out) const;

    // s_room_info's per-frame flags.
    bool RoomRendered(int room) const;
    bool RoomNeighborToRendered(int room) const;
    // Turned off by the stage's visibility commands.
    bool RoomDisabled(int room) const;

    // The rooms the stage's visibility commands added by name this frame
    // (list_visible_rooms_in_cur_global_vis_packet), which bgCopyVisibleRoomsToList
    // hands out. The game clears it at the start of bgTick.
    const std::vector<uint8_t>& CommandRooms() const {
        return commandRooms_;
    }
    void ClearCommandRooms() {
        commandRooms_.clear();
    }

    // The frame counter (D_80044858, 0-9) that staggers which rooms the
    // renderer refreshes each frame.
    int FrameStagger() const {
        return frameStagger_;
    }

    // bgIsRoomOnScreen: false when the room's box is entirely off one side of
    // `rect` or behind the camera.
    bool IsRoomOnScreen(int room, const ScreenRect& rect) const;

  private:
    struct QueuedPortal {
        int room = 0;
        int portal = 0;
        int depth = 0;
        ScreenRect bounds;
    };

    // Memoized bgGetPortalScreenBounds results (table_for_portals); count < 0
    // means not worked out yet this frame.
    struct PortalBounds {
        int count = -1;
        ScreenRect rect;
    };

    struct Command {
        uint8_t type;
        uint8_t length;
        int32_t arg;
    };

    // bgProjectRoomCoordToScreen: projects a room-space point; false if it is
    // behind the camera.
    bool ProjectRoomCoord(const Vec3& point, Vec3& out) const;
    // sub_GAME_7F0B5528: the portal's points, offset along its normal by
    // `offset`, in camera space, with the ones crossing the near plane
    // clipped in. Empty when every point is behind the camera.
    std::vector<Vec3> ProjectPortalPoints(size_t portal, float offset) const;
    // sub_GAME_7F0B5864: the portal's on-screen area; the number of its
    // points in front of the camera (0 if none).
    int PortalScreenBounds(size_t portal, ScreenRect& out);

    // sub_GAME_7F0B39BC: adds `room` to the draw list, or widens its entry.
    // Returns the entry's `next` from before, 0 for a new one.
    int AddRoom(int room, int depth, ScreenRect& bounds, int next);
    void ResetPortalVisitCounts();
    int IncrementPortalVisitCount(int room);

    void ResetPortalQueue();
    void QueuePortalTraversal(int room, int portal, int depth, const ScreenRect& bounds);
    bool ProcessNextQueuedPortal();
    // sub_GAME_7F0B7F84 (bgPortalDescend).
    void DescendPortal(int room, int portal, int depth, const ScreenRect& parentBounds);

    // sub_GAME_7F0B8A24 and parse_global_vis_command_list.
    void RunVisibilityCommands();
    // Returns the index of the record it stopped at.
    size_t ParseCommands(size_t at, int execute);
    bool ReadCommand(size_t at, Command& out) const;

    void StackPush(int32_t value);
    int32_t StackPop();

    BgLevel& level_;
    VisibilityView view_;

    std::vector<RoomDraw> drawn_;
    std::array<uint8_t, kMaxRoomCount> portalVisits_{};
    std::array<bool, kMaxRoomCount> rendered_{};
    std::array<bool, kMaxRoomCount> neighborToRendered_{};
    std::array<bool, kMaxRoomCount> disabled_{};
    std::vector<uint8_t> commandRooms_;
    std::vector<PortalBounds> portalBounds_;
    int frameStagger_ = 0;

    std::vector<QueuedPortal> queue_;
    size_t queueRead_ = 0;
    size_t queueWrite_ = 0;

    // The commands' operand stack, which the game never clears between frames.
    std::array<int32_t, 20> stack_{};
    int stackCount_ = 0;
    // current_visibility: nonzero while no command has fixed the room's area.
    int currentVisibility_ = 0;
    // The area the commands are building (dword_80081600).
    ScreenRect commandBounds_;
    bool skipExecOnReturn_ = false;
};

} // namespace Frigate::Level
