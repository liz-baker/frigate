#include "level/BgVisibility.h"

namespace Frigate::Level {

namespace {

constexpr size_t kRecordSize = 8;
// BG_PORTAL_QUEUE_LEN (US).
constexpr size_t kPortalQueueLength = 500;
// A portal is not followed deeper than this (D_8004489C).
constexpr int kMaxPortalDepth = 0xF;
// The most times a room's portals are followed past the second level
// before it stops being queued.
constexpr int kMaxRoomVisits = 9;
constexpr int kCradleRoom = 9;
// The most portals Update follows from the player's room in one frame.
constexpr int kMaxCrossingDepth = 11;

// GlobalVisOpcode.
enum VisOp : uint8_t {
    End = 0x00,
    Push = 0x01,
    Pop = 0x02,
    And = 0x03,
    Or = 0x04,
    Not = 0x05,
    Xor = 0x06,
    PushIfRoomInRange = 0x14,
    ForceVisible = 0x1E,
    MatchPortalVis = 0x1F,
    AddVisibleRoom = 0x20,
    RemoveVis = 0x21,
    VisibleIfSeenThroughPortal = 0x22,
    NotVisibleIfSeenThroughPortal = 0x23,
    DisableRoom = 0x24,
    DisableRoomRange = 0x25,
    PreloadRoom = 0x26,
    PreloadRoomRange = 0x27,
    IfStatement = 0x50,
    DontExecCommandsEvenOnReturn = 0x51,
    EndifContinueExec = 0x52,
    IfStatementPullFromStack = 0x5A,
    ToggleExecVsReadonly = 0x5B,
    Endif = 0x5C,
};

int32_t ReadS32(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<int32_t>((uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
                                (uint32_t(bytes[at + 2]) << 8) | uint32_t(bytes[at + 3]));
}

bool ValidRoom(int room) {
    return room >= 0 && room < kMaxRoomCount;
}

} // namespace

bool RectIntersect(ScreenRect& a, const ScreenRect& b) {
    a.left = a.left > b.left ? a.left : b.left;
    a.top = a.top > b.top ? a.top : b.top;
    a.right = b.right > a.right ? a.right : b.right;
    a.bottom = b.bottom > a.bottom ? a.bottom : b.bottom;

    if (a.left >= a.right) {
        a.left = a.right;
        return false;
    }
    if (a.bottom <= a.top) {
        a.top = a.bottom;
        return false;
    }
    return true;
}

void RectUnion(ScreenRect& a, const ScreenRect& b) {
    a.left = a.left < b.left ? a.left : b.left;
    a.top = a.top < b.top ? a.top : b.top;
    a.right = a.right > b.right ? a.right : b.right;
    a.bottom = a.bottom > b.bottom ? a.bottom : b.bottom;
}

bool RectIsInside(const ScreenRect& inner, const ScreenRect& outer) {
    return outer.left <= inner.left && inner.left <= outer.right && outer.top <= inner.top && inner.top <= outer.bottom;
}

BgVisibility::BgVisibility(BgLevel& level) : level_(level) {
    queue_.resize(kPortalQueueLength);
}

void BgVisibility::SetRoomDepth(int room, int depth) {
    for (RoomDraw& draw : drawn_) {
        if (draw.room == room) {
            draw.depth = depth;
            return;
        }
    }
}

bool BgVisibility::GetRoomBounds2d(int room, ScreenRect& out) const {
    for (const RoomDraw& draw : drawn_) {
        if (draw.room == room) {
            out = draw.bounds;
            return true;
        }
    }
    out = {};
    return false;
}

bool BgVisibility::RoomRendered(int room) const {
    return ValidRoom(room) && rendered_[room];
}

bool BgVisibility::RoomNeighborToRendered(int room) const {
    return ValidRoom(room) && neighborToRendered_[room];
}

bool BgVisibility::RoomDisabled(int room) const {
    return ValidRoom(room) && disabled_[room];
}

bool BgVisibility::ProjectRoomCoord(const Vec3& point, Vec3& out) const {
    const float scale = 1.0f / level_.Level().scale;
    const auto& m = view_.worldToScreen;

    Vec3 in{ point[0] * scale, point[1] * scale, point[2] * scale };
    for (int i = 0; i < 3; i++) {
        out[i] = m[0][i] * in[0] + m[1][i] * in[1] + m[2][i] * in[2] + m[3][i];
    }

    // transform3Dto2DWithZScaling: x and y become screen coordinates, z stays
    // the camera-space depth.
    float inverseZ = out[2] == 0.0f ? -100000000000000000000.0f : 1.0f / out[2];
    out[1] = out[1] * inverseZ * view_.recipScaleY + (view_.screenTop + view_.halfHeight);
    out[0] = (view_.screenLeft + view_.halfWidth) - out[0] * inverseZ * view_.recipScaleX;

    return !(out[2] > 0.0f);
}

bool BgVisibility::IsRoomOnScreen(int room, const ScreenRect& rect) const {
    if (!ValidRoom(room)) {
        return false;
    }
    const RoomInfo& info = level_.Room(room);
    const float zFar = view_.zFar / level_.VisibilityScale();

    int behind = 0;
    int failedProjection = 0;
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    for (int i = 0; i < 8; i++) {
        Vec3 corner{ (i & 1) ? info.minBounds[0] : info.maxBounds[0], (i & 2) ? info.minBounds[1] : info.maxBounds[1],
                     (i & 4) ? info.minBounds[2] : info.maxBounds[2] };
        Vec3 projected;
        bool inFront = ProjectRoomCoord(corner, projected);

        if (zFar <= -projected[2]) {
            behind++;
        }
        if (!inFront) {
            if (rect.left <= projected[0]) {
                left++;
            }
            if (projected[0] <= rect.right) {
                right++;
            }
            if (rect.top <= projected[1]) {
                top++;
            }
            if (projected[1] <= rect.bottom) {
                bottom++;
            }
            failedProjection++;
        } else {
            if (projected[0] <= rect.left) {
                left++;
            } else if (rect.right <= projected[0]) {
                right++;
            }
            if (projected[1] <= rect.top) {
                top++;
            } else if (rect.bottom <= projected[1]) {
                bottom++;
            }
        }
    }

    return !(failedProjection == 8 || behind == 8 || left == 8 || right == 8 || top == 8 || bottom == 8);
}

std::vector<Vec3> BgVisibility::ProjectPortalPoints(size_t index, float offset) const {
    const BgPortal& portal = level_.Portals()[index];
    const float scale = 1.0f / level_.Level().scale;
    const float zFar = view_.zFar / level_.VisibilityScale();
    const auto& m = view_.worldToScreen;

    std::vector<Vec3> points;
    bool allBehind = true;
    for (const Vec3& source : portal.points) {
        Vec3 point = source;
        if (offset != 0.0f) {
            PortalMetric metric = MeasurePortal(portal);
            for (int i = 0; i < 3; i++) {
                point[i] += metric.normal[i] * offset;
            }
        }
        for (float& component : point) {
            component *= scale;
        }
        Vec3 camera;
        for (int i = 0; i < 3; i++) {
            camera[i] = m[0][i] * point[0] + m[1][i] * point[1] + m[2][i] * point[2] + m[3][i];
        }
        if (-zFar * 0.9f < camera[2]) {
            allBehind = false;
        }
        points.push_back(camera);
    }
    if (allBehind) {
        return {};
    }

    // Points where an edge crosses the camera plane join the polygon.
    const size_t count = points.size();
    for (size_t i = 0; i < count; i++) {
        const Vec3 point = points[i];
        const Vec3 next = points[(i + 1) % count];
        if ((point[2] > 0.0f && next[2] <= 0.0f) || (point[2] <= 0.0f && next[2] > 0.0f)) {
            float t = -point[2] / (next[2] - point[2]);
            points.push_back({ point[0] + (next[0] - point[0]) * t, point[1] + (next[1] - point[1]) * t, 0.0f });
        }
    }
    return points;
}

int BgVisibility::PortalScreenBounds(size_t index, ScreenRect& out) {
    if (index >= portalBounds_.size()) {
        out = {};
        return 0;
    }
    PortalBounds& cache = portalBounds_[index];
    if (cache.count >= 0) {
        out = cache.rect;
        return cache.count;
    }

    const BgPortal& portal = level_.Portals()[index];
    const float padding = PortalPadding(portal);
    std::vector<Vec3> points = ProjectPortalPoints(index, padding);
    if (padding > 0.0f) {
        std::vector<Vec3> behind = ProjectPortalPoints(index, -padding);
        points.insert(points.end(), behind.begin(), behind.end());
    }

    ScreenRect bounds;
    int onScreen = 0;
    for (Vec3& point : points) {
        if (point[2] > 0.0f) {
            continue;
        }
        float inverseZ = point[2] == 0.0f ? -100000000000000000000.0f : 1.0f / point[2];
        float y = point[1] * inverseZ * view_.recipScaleY + (view_.screenTop + view_.halfHeight);
        float x = (view_.screenLeft + view_.halfWidth) - point[0] * inverseZ * view_.recipScaleX;

        if (onScreen == 0) {
            bounds.left = bounds.right = x;
            bounds.top = bounds.bottom = y;
        } else {
            bounds.left = x < bounds.left ? x : bounds.left;
            bounds.right = bounds.right < x ? x : bounds.right;
            bounds.top = y < bounds.top ? y : bounds.top;
            bounds.bottom = bounds.bottom < y ? y : bounds.bottom;
        }
        onScreen++;
    }

    if (onScreen == 0) {
        bounds = {};
    } else if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        bounds = view_.screen;
    }

    cache.rect = bounds;
    cache.count = onScreen;
    out = bounds;
    return onScreen;
}

int BgVisibility::AddRoom(int room, int depth, ScreenRect& bounds, int next) {
    if (!ValidRoom(room)) {
        return 0;
    }
    rendered_[room] = true;
    if (disabled_[room]) {
        return 0;
    }

    for (RoomDraw& draw : drawn_) {
        if (draw.room != room) {
            continue;
        }
        if (draw.depth < depth) {
            draw.depth = depth;
        }
        RectUnion(bounds, draw.bounds);
        int previous = draw.next;
        draw.bounds = bounds;
        draw.next = previous | next;
        return previous;
    }

    drawn_.push_back({ room, depth, bounds, next });
    return 0;
}

void BgVisibility::ResetPortalVisitCounts() {
    drawn_.clear();
    portalVisits_.fill(0);
}

int BgVisibility::IncrementPortalVisitCount(int room) {
    if (!ValidRoom(room)) {
        return 0;
    }
    uint8_t& count = portalVisits_[room];
    if (count < 0xFF) {
        count++;
    }
    return count;
}

void BgVisibility::ResetPortalQueue() {
    queueWrite_ = 0;
    queueRead_ = 0;
}

void BgVisibility::QueuePortalTraversal(int room, int portal, int depth, const ScreenRect& bounds) {
    if (depth >= 2) {
        const BgPortal& p = level_.Portals()[portal];
        int other = (p.room2 ^ p.room1) ^ room;
        if (IncrementPortalVisitCount(other) >= kMaxRoomVisits) {
            return;
        }
    }
    queue_[queueWrite_] = { room, portal, depth, bounds };
    queueWrite_++;
    if (queueWrite_ == kPortalQueueLength) {
        queueWrite_ = 0;
    }
    // A full queue drops its newest entry rather than overwrite the oldest.
    if (queueWrite_ == queueRead_) {
        queueWrite_ = (queueWrite_ + kPortalQueueLength - 1) % kPortalQueueLength;
    }
}

bool BgVisibility::ProcessNextQueuedPortal() {
    if (queueRead_ == queueWrite_) {
        return false;
    }
    const QueuedPortal entry = queue_[queueRead_];
    DescendPortal(entry.room, entry.portal, entry.depth, entry.bounds);

    queueRead_++;
    if (queueRead_ == kPortalQueueLength) {
        queueRead_ = 0;
    }
    return true;
}

void BgVisibility::DescendPortal(int room, int index, int depth, const ScreenRect& parentBounds) {
    if (depth > kMaxPortalDepth) {
        return;
    }

    const BgPortal& portal = level_.Portals()[index];
    if (portal.controlBytes1 & kPortalDisabled) {
        return;
    }

    PortalMetric metric = MeasurePortal(portal);
    const float roomScale = level_.Level().scale;
    const float playerMetric = (metric.normal[2] * view_.position[2] +
                                (metric.normal[0] * view_.position[0] + metric.normal[1] * view_.position[1])) *
                               roomScale;
    const float padding = PortalPadding(portal);

    int otherRoom;
    if (room == portal.room1) {
        otherRoom = portal.room2;
        if (metric.max <= playerMetric - padding) {
            return;
        }
    } else {
        otherRoom = portal.room1;
        if (playerMetric + padding <= metric.min) {
            return;
        }
    }

    ScreenRect bounds;
    if ((metric.min - padding) < playerMetric && playerMetric < (metric.max + padding)) {
        bounds = view_.screen;
    } else {
        if (portal.controlBytes1 & kPortalSpecial) {
            if (!PortalScreenBounds(index, bounds)) {
                return;
            }
            otherRoom = (portal.room1 ^ portal.room2) ^ room;
            if (!IsRoomOnScreen(otherRoom, bounds)) {
                return;
            }
            bounds = view_.screen;
        } else {
            if (!PortalScreenBounds(index, bounds)) {
                return;
            }
            RectIntersect(bounds, parentBounds);
            RectIntersect(bounds, view_.screen);
        }

        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
            return;
        }
    }

    if (!(bounds.left < bounds.right && bounds.top < bounds.bottom)) {
        return;
    }
    if (AddRoom(otherRoom, depth, bounds, portal.controlBytes1 & kPortalSpecial)) {
        return;
    }

    const std::vector<BgPortal>& portals = level_.Portals();
    for (size_t i = 0; i < portals.size(); i++) {
        if (static_cast<int>(i) != index && (otherRoom == portals[i].room1 || otherRoom == portals[i].room2)) {
            QueuePortalTraversal(otherRoom, static_cast<int>(i), depth + 1, bounds);
        }
    }
}

void BgVisibility::StackPush(int32_t value) {
    stack_[stackCount_] = value;
    stackCount_ = (stackCount_ + 1) % static_cast<int>(stack_.size());
}

int32_t BgVisibility::StackPop() {
    const int size = static_cast<int>(stack_.size());
    stackCount_ = (stackCount_ + size - 1) % size;
    return stack_[stackCount_];
}

bool BgVisibility::ReadCommand(size_t at, Command& out) const {
    const std::vector<uint8_t>& bytes = level_.Header().bytes;
    if (at + kRecordSize > bytes.size()) {
        return false;
    }
    out.type = bytes[at];
    out.length = bytes[at + 1];
    out.arg = ReadS32(bytes, at + 4);
    return true;
}

void BgVisibility::RunVisibilityCommands() {
    currentVisibility_ = 0;
    if (level_.Header().visibilityOffset == 0) {
        return;
    }
    ParseCommands(level_.Header().visibilityOffset, 1);
}

size_t BgVisibility::ParseCommands(size_t at, int execute) {
    bool preloadOk = true;
    skipExecOnReturn_ = false;

    while (true) {
        Command cmd;
        if (!ReadCommand(at, cmd)) {
            return at;
        }
        // A record that does not advance would hold the loop forever.
        if (cmd.length == 0 && cmd.type != End) {
            return at;
        }
        const size_t next = at + static_cast<size_t>(cmd.length) * kRecordSize;
        // The record after this one, which holds the operands of the commands
        // that take a room or portal.
        Command operand{};
        Command operandEnd{};
        ReadCommand(at + kRecordSize, operand);
        ReadCommand(at + 2 * kRecordSize, operandEnd);

        switch (cmd.type) {
            case End:
                return at;

            case Push:
                if (execute) {
                    StackPush(cmd.arg);
                }
                at = next;
                break;

            case Pop:
                if (execute) {
                    StackPop();
                }
                at = next;
                break;

            case And:
                if (execute) {
                    int32_t value = StackPop();
                    StackPush(StackPop() & value);
                }
                at = next;
                break;

            case Or:
                if (execute) {
                    int32_t value = StackPop();
                    StackPush(StackPop() | value);
                }
                at = next;
                break;

            case Not:
                if (execute) {
                    StackPush(StackPop() == 0);
                }
                at = next;
                break;

            case Xor:
                if (execute) {
                    int32_t value = StackPop();
                    StackPush(StackPop() ^ value);
                }
                at = next;
                break;

            case PushIfRoomInRange:
                if (execute) {
                    StackPush(operand.arg <= view_.currentRoom && view_.currentRoom <= operandEnd.arg);
                }
                at = next;
                break;

            case ForceVisible:
                if (execute) {
                    commandBounds_ = view_.screen;
                    currentVisibility_ = 0;
                }
                at = next;
                break;

            case MatchPortalVis:
                if (execute) {
                    if (PortalScreenBounds(operand.arg, commandBounds_) == 0) {
                        currentVisibility_ = 1;
                    } else if (!RectIntersect(commandBounds_, view_.screen)) {
                        currentVisibility_ = 1;
                    } else {
                        currentVisibility_ = 0;
                    }
                }
                at = next;
                break;

            case VisibleIfSeenThroughPortal:
                if (execute) {
                    ScreenRect seen;
                    if (PortalScreenBounds(operand.arg, seen) && RectIntersect(seen, view_.screen)) {
                        if (currentVisibility_) {
                            commandBounds_ = seen;
                            currentVisibility_ = 0;
                        } else {
                            RectUnion(commandBounds_, seen);
                        }
                    }
                }
                at = next;
                break;

            case NotVisibleIfSeenThroughPortal:
                if (execute && !currentVisibility_) {
                    ScreenRect seen;
                    if (!PortalScreenBounds(operand.arg, seen)) {
                        currentVisibility_ = 1;
                    } else if (!RectIntersect(seen, view_.screen)) {
                        currentVisibility_ = 1;
                    } else if (!RectIntersect(commandBounds_, seen)) {
                        currentVisibility_ = 1;
                    }
                }
                at = next;
                break;

            case AddVisibleRoom:
                if (execute && !currentVisibility_) {
                    if (IsRoomOnScreen(operand.arg, commandBounds_)) {
                        AddRoom(operand.arg, 0, commandBounds_, 0);
                        commandRooms_.push_back(static_cast<uint8_t>(operand.arg));
                    }
                }
                at = next;
                break;

            case DisableRoom:
                if (execute && ValidRoom(operand.arg)) {
                    disabled_[operand.arg] = true;
                }
                at = next;
                break;

            case DisableRoomRange:
                if (execute) {
                    for (int room = operand.arg; room <= operandEnd.arg; room++) {
                        if (ValidRoom(room)) {
                            disabled_[room] = true;
                        }
                    }
                }
                at = next;
                break;

            case PreloadRoom:
                if (execute && preloadOk) {
                    preloadOk = !level_.CheckIfRoomModelNeedsLoad(operand.arg);
                }
                at = next;
                break;

            case PreloadRoomRange:
                if (execute) {
                    for (int room = operand.arg; room <= operandEnd.arg; room++) {
                        if (preloadOk) {
                            preloadOk = !level_.CheckIfRoomModelNeedsLoad(room);
                        }
                    }
                }
                at = next;
                break;

            case RemoveVis:
                if (execute) {
                    currentVisibility_ = 1;
                }
                at = next;
                break;

            case IfStatement: {
                size_t returned = ParseCommands(next, execute);
                Command last;
                if (ReadCommand(returned, last)) {
                    returned += static_cast<size_t>(last.length) * kRecordSize;
                }
                at = returned;
                break;
            }

            case EndifContinueExec:
                skipExecOnReturn_ = false;
                return next;

            case DontExecCommandsEvenOnReturn:
                at = next;
                if (execute) {
                    execute = 0;
                    skipExecOnReturn_ = true;
                } else {
                    execute = 0;
                }
                break;

            case IfStatementPullFromStack: {
                int32_t value = StackPop();
                at = ParseCommands(next, value & execute);
                if (skipExecOnReturn_) {
                    execute = 0;
                }
                break;
            }

            case ToggleExecVsReadonly:
                execute ^= 1;
                at = next;
                break;

            case Endif:
                return next;

            default:
                return at;
        }
    }
}

int BgVisibility::Update(VisibilityView view, const Vec3& previousPosition, CameraMode cameraMode, bool firstPlayer) {
    commandRooms_.clear();
    if (firstPlayer) {
        level_.TickUnload();
    }

    // The modes that show the whole stage (intro, swirl, multiplayer) load
    // every room they need at once.
    switch (cameraMode) {
        case CameraMode::Intro:
        case CameraMode::FadeSwirl:
        case CameraMode::Swirl:
        case CameraMode::PosEnd:
        case CameraMode::MultiPlayer:
            level_.SetRoomLoadBudget(0xC8);
            break;
        default:
            level_.SetRoomLoadBudget(3);
            break;
    }

    const size_t portalCount = level_.Portals().size();
    int room = view.currentRoom;
    int lastPortal = -1;
    for (int depth = 0; depth != kMaxCrossingDepth; depth++) {
        size_t portal = 0;
        for (; portal < portalCount; portal++) {
            if (level_.PortalNormalIsHorizontal(portal) || static_cast<int>(portal) == lastPortal) {
                continue;
            }
            const BgPortal& entry = level_.Portals()[portal];
            if ((room == entry.room1 || room == entry.room2) &&
                level_.PortalCrossing(portal, view.position, previousPosition) != 0) {
                lastPortal = static_cast<int>(portal);
                room = (room ^ entry.room1) ^ entry.room2;
                break;
            }
        }
        if (portal == portalCount) {
            break;
        }
    }

    view.currentRoom = room;
    DetermineVisibleRooms(view);
    return room;
}

void BgVisibility::DetermineVisibleRooms(const VisibilityView& view) {
    view_ = view;
    const std::vector<BgPortal>& portals = level_.Portals();

    ResetPortalVisitCounts();
    rendered_.fill(false);
    neighborToRendered_.fill(false);
    disabled_.fill(false);
    frameStagger_ = (frameStagger_ + 1) % 10;

    ResetPortalQueue();
    portalBounds_.assign(portals.size(), PortalBounds{});
    RunVisibilityCommands();

    // Cradle skips occlusion culling, as does a stage with no portals: every
    // room in the view is drawn.
    if (level_.Level().id == LevelId::Cradle || portals.empty()) {
        if (level_.Level().id == LevelId::Cradle) {
            ScreenRect bounds = view_.screen;
            AddRoom(kCradleRoom, 0, bounds, 1);
        }
        for (int room = 1; room < level_.MaxNumRooms(); room++) {
            if (IsRoomOnScreen(room, view_.screen)) {
                ScreenRect bounds = view_.screen;
                AddRoom(room, 0, bounds, 1);
            }
        }
    } else {
        ScreenRect bounds = view_.screen;
        AddRoom(view_.currentRoom, 0, bounds, 1);

        for (size_t i = 0; i < portals.size(); i++) {
            if (view_.currentRoom == portals[i].room1 || view_.currentRoom == portals[i].room2) {
                QueuePortalTraversal(view_.currentRoom, static_cast<int>(i), 1, view_.screen);
            }
        }
        while (ProcessNextQueuedPortal()) {}
    }

    for (const BgPortal& portal : portals) {
        const int room1 = portal.room1;
        const int room2 = portal.room2;
        if (!ValidRoom(room1) || !ValidRoom(room2)) {
            continue;
        }
        if (rendered_[room1] && !rendered_[room2]) {
            neighborToRendered_[room2] = true;
        } else if (rendered_[room2] && !rendered_[room1]) {
            neighborToRendered_[room1] = true;
        }
    }
}

} // namespace Frigate::Level
