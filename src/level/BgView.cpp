#include "level/BgView.h"

#include <algorithm>
#include <cmath>

namespace Frigate::Level {

ScreenRect ScreenMinMax(const ViewMetrics& vi, const std::array<int32_t, 4>& viewRelated) {
    const auto fx = static_cast<float>(viewRelated[0]);
    const auto fy = static_cast<float>(viewRelated[1]);
    const auto fwidth = static_cast<float>(vi.x) + static_cast<float>(viewRelated[2]);
    const auto fheight = static_cast<float>(vi.y) + static_cast<float>(viewRelated[3]);

    ScreenRect out;
    out.left = static_cast<float>(vi.viewLeft);
    out.left = std::min(std::max(out.left, fx), fwidth);
    out.top = static_cast<float>(vi.viewTop);
    out.top = std::min(std::max(out.top, fy), fheight);
    out.right = static_cast<float>(vi.viewLeft + vi.viewWidth);
    out.right = std::min(std::max(out.right, fx), fwidth);
    out.bottom = static_cast<float>(vi.viewTop + vi.viewHeight);
    out.bottom = std::min(std::max(out.bottom, fy), fheight);
    return out;
}

Scissor ClipScissor(const PlayerView& view, int32_t left, int32_t top, int32_t right, int32_t bottom) {
    return { std::max(left, view.left), std::max(top, view.top), std::min(right, view.left + view.width),
             std::min(bottom, view.top + view.height) };
}

Scissor ViewScissor(const PlayerView& view) {
    return ClipScissor(view, view.left, view.top, view.left + view.width, view.top + view.height);
}

Scissor BoundsScissor(const PlayerView& view, const ScreenRect& bounds) {
    return ClipScissor(view, static_cast<int32_t>(bounds.left), static_cast<int32_t>(bounds.top),
                       static_cast<int32_t>(std::ceil(bounds.right)), static_cast<int32_t>(std::ceil(bounds.bottom)));
}

void RoomSlots::Reset(int roomCount) {
    room_.fill(-1);
    status_.fill(2);
    owner_.fill(-1);
    roomSlot_.assign(static_cast<size_t>(roomCount), -1);
}

int RoomSlots::FindFree() const {
    for (int i = 0; i < kSlotCount; i++) {
        if (status_[i] >= 2 && owner_[i] == -1) {
            return i;
        }
    }
    return 0;
}

void RoomSlots::ResetSlot(int slot) {
    if (room_[slot] != -1) {
        roomSlot_[room_[slot]] = -1;
        room_[slot] = -1;
    }
    status_[slot] = 2;
    owner_[slot] = -1;
}

int RoomSlots::Acquire(int room, int currentRoom, bool& rebuild) {
    int slot = roomSlot_[room];

    if (slot != -1 && currentRoom == owner_[slot]) {
        status_[slot] = 0;
        rebuild = false;
        return slot;
    }

    if (slot != -1) {
        roomSlot_[room] = -1;
        room_[slot] = -1;
    }
    slot = FindFree();
    roomSlot_[room] = slot;
    room_[slot] = room;
    status_[slot] = 0;
    owner_[slot] = currentRoom;
    rebuild = true;
    return slot;
}

void RoomSlots::Tick() {
    for (int i = 0; i < kSlotCount; i++) {
        if (owner_[i] > -1 && ++status_[i] >= 2) {
            ResetSlot(i);
        }
    }
}

void RoomTransform(const Vec3& roomPosition, float roomScale, const Vec3& modelPos, float (&out)[4][4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
    out[0][0] = roomScale;
    out[1][1] = roomScale;
    out[2][2] = roomScale;
    for (int axis = 0; axis < 3; axis++) {
        out[3][axis] = roomPosition[axis] * roomScale - modelPos[axis];
    }
}

} // namespace Frigate::Level
