#include <algorithm>
#include <cmath>
#include <limits>

#include "level/ApplySpeed.h"
#include "level/ChrActions.h"
#include "model/Random.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;

constexpr float DegToRad(int degrees) {
    return static_cast<float>(degrees) * kTau / 360.0f;
}

// chrlvApplySpeed's turn speeds and accelerations, in radians per tick.
constexpr float kMaxSpeedA = 0.2991993f;
constexpr float kAccelA = 0.014959966f;
constexpr float kMaxSpeedB1 = 0.019634955f;
constexpr float kMaxSpeedB2 = 0.09817477f;
constexpr float kMaxSpeedB3 = 0.19634955f;
constexpr float kAccelB = 0.014959966f;
constexpr float kMaxSpeedC1 = 0.009817477f;
constexpr float kMaxSpeedC2 = 0.049087387f;
constexpr float kMaxSpeedC3 = 0.12566371f;
constexpr float kAccelC = 0.009817477f;

// CHRLV_DEFAULT_TIMER and CHRLV_LASTMOVEOK60_CHECK, and the number of guards
// that have seen the player (g_SeenBondRecentlyGuardCount) at which the rest
// stop moving.
constexpr int kDefaultTimer = 180;
constexpr int kLastMoveOkCheck = 60;
constexpr int kSeenBondLimit = 10;

// The tests of a route's steps.
constexpr int kBodies = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;

// How close to a waypoint a patrolling or travelling character counts as having reached it.
constexpr float kArrivalRange = 30.0f;

Frigate::Vec3 ToPlayback(const Vec3& v) {
    return { v[0], v[1], v[2] };
}

// The squared length of v in the floor plane.
float PlaneLengthSquared(float x, float z) {
    return (x * x) + (z * z);
}

// A float to s32 conversion that saturates, as the console's does.
int32_t ToInt(float value) {
    if (!(value < 2147483648.0f)) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value < -2147483648.0f) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(value);
}

} // namespace

// -- Small queries ------------------------------------------------------------------

Prop& ChrActions::PropOf(const Chr& chr) const {
    return (*scene_.props)[chr.prop];
}

int ChrActions::SlotOf(const Chr& chr) const {
    return static_cast<int>(&chr - scene_.chrs->Slots().data());
}

Vec3 ChrActions::PatrolPercentOrPosition(const Chr& chr) const {
    const Prop& prop = PropOf(chr);
    const auto along = [&](const Vec3& target, const Chr::WayData& way) {
        if (way.segDistTotal <= way.segDistDone) {
            return target;
        }
        const float percent = way.segDistDone / way.segDistTotal;
        return Vec3{ prop.position[0] + ((target[0] - prop.position[0]) * percent),
                     prop.position[1] + ((target[1] - prop.position[1]) * percent),
                     prop.position[2] + ((target[2] - prop.position[2]) * percent) };
    };
    if (chr.actionType == ChrAction::Patrol && chr.actPatrol.way.mode == WayMode::Magic) {
        return along(PatrolPad(chr, 0).position, chr.actPatrol.way);
    }
    if (chr.actionType == ChrAction::GoPos && chr.actGoPos.way.mode == WayMode::Magic) {
        return along(GoPosTarget(chr).position, chr.actGoPos.way);
    }
    return prop.position;
}

ChrActions::Step ChrActions::GoPosTarget(const Chr& chr) const {
    // chrlvActGoposRelated.
    const int waypoint = chr.actGoPos.waypoints[chr.actGoPos.currentIndex];
    if (waypoint >= 0) {
        const Pad& pad = scene_.paths->PadOf(waypoint);
        return { pad.position, pad.stanTile };
    }
    return { chr.actGoPos.targetPosition, chr.actGoPos.targetTile };
}

bool ChrActions::WithinRangeAhead(const Vec3& origin, const Vec3& direction, const Vec3& target, float range) {
    // chrlvGeometryRelated7F02FC34.
    const float dx = target[0] - origin[0];
    const float dz = target[2] - origin[2];

    if (direction[0] == 0.0f && direction[2] == 0.0f) {
        return ((dx * dx) + (dz * dz)) <= (range * range);
    }

    const float along = (direction[0] * dx) + (direction[2] * dz);
    if (along > 0.0f) {
        const float alongSquared = along * along;
        const float directionSquared = (direction[0] * direction[0]) + (direction[2] * direction[2]);
        const float distanceSquared = (dx * dx) + (dz * dz);
        return (directionSquared * (distanceSquared - (range * range))) <= alongSquared;
    }
    return false;
}

bool ChrActions::ArrivingAt(const Vec3& previous, const Vec3& current, const Vec3& target, float range) {
    // chrlvIsArrivingLaterallyAtPos.
    if (previous[0] <= target[0] - range && current[0] <= target[0] - range) {
        return false;
    }
    if (target[0] + range <= previous[0] && target[0] + range <= current[0]) {
        return false;
    }
    if (previous[2] <= target[2] - range && current[2] <= target[2] - range) {
        return false;
    }
    if (target[2] + range <= previous[2] && target[2] + range <= current[2]) {
        return false;
    }

    const Vec3 direction = { current[0] - previous[0], 0.0f, current[2] - previous[2] };
    return WithinRangeAhead(previous, direction, target, range);
}

bool ChrActions::OffScreenRoute(Chr& chr, const Vec3& position, int tile) {
    // chrlvStanRoomRelated: the line to `position` crosses no room that is drawn.
    const Prop& prop = PropOf(chr);
    std::array<int, 20> rooms{};
    int destination = tile;
    const int count = scene_.stan->RoomsBetween(prop.stan, prop.position[0], prop.position[2], &destination,
                                                position[0], position[2], rooms.data(), static_cast<int>(rooms.size()),
                                                [this](int a, int b) { return host_.RoomsSharePortal(a, b); });

    if (count > 0 && count < static_cast<int>(rooms.size())) {
        for (int i = 0; i < count; i++) {
            if (host_.RoomRendered(rooms[static_cast<size_t>(i)])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool ChrActions::ClearOfObjects(const Vec3& position, int tile) {
    // chrlvIsPosClearOfObjectBounds: every object in the tile's room counts,
    // whatever it collides with.
    const std::array<int, 2> rooms = { scene_.stan->TileRoom(tile), -1 };
    std::vector<PropCollider> colliders;
    scene_.objects->Colliders(rooms.data(), CdType::Objs | CdType::PathBlocker, &colliders);
    for (const PropCollider& collider : colliders) {
        if (collider.edges > 0 && PointInPolygon(position, collider.polygon, collider.edges)) {
            return false;
        }
    }
    return true;
}

float ChrActions::ClearDistance(Chr& chr, float angle, float distance, int cdTypes, float height, float above) {
    // chrlvPathingCollisionRelated.
    const Prop& prop = PropOf(chr);
    int tile = prop.stan;

    const Vec3 direction = { std::sin(angle), 0.0f, std::cos(angle) };
    const float destX = prop.position[0] + (direction[0] * distance);
    const float destZ = prop.position[2] + (direction[2] * distance);

    ChrAi::SetMoving(chr, false);
    scene_.stan->ResetHits();

    float result;
    if (scene_.stan->TestLineUnobstructed(&tile, prop.position[0], prop.position[2], destX, destZ, cdTypes, height,
                                          above, 0.0f, 1.0f) != 0) {
        result = distance;
    } else {
        // chrlvStanLineDirIntersection: where the ray meets the edge it hit.
        Vec3 edgeA{};
        Vec3 edgeB{};
        scene_.stan->CollisionEdge(&edgeA, &edgeB);
        const Vec3 hit = LineLineIntersection(edgeA, edgeB, prop.position, direction);
        const float dx = hit[0] - prop.position[0];
        const float dz = hit[2] - prop.position[2];
        result = std::sqrt((dx * dx) + (dz * dz));
    }

    ChrAi::SetMoving(chr, true);
    return result;
}

Vec3 ChrActions::LineLineIntersection(const Vec3& line1A, const Vec3& line1B, const Vec3& point,
                                      const Vec3& direction) {
    // chrlvLineLineIntersection.
    const float denominator = (direction[2] * (line1B[0] - line1A[0])) - (direction[0] * (line1B[2] - line1A[2]));

    if (denominator != 0.0f) {
        const float v =
            (((line1A[2] - point[2]) * (line1B[0] - line1A[0])) + ((point[0] - line1A[0]) * (line1B[2] - line1A[2]))) /
            denominator;
        return { point[0] + (direction[0] * v), point[1] + (direction[1] * v), point[2] + (direction[2] * v) };
    }
    if (direction[0] == 0.0f && direction[2] == 0.0f) {
        return point;
    }
    return line1A;
}

// -- Steering -------------------------------------------------------------------------

bool ChrActions::TurnTowards(Chr& chr, const Vec3& target, int mode, float* speed) {
    // chrlvApplySpeed.
    const Prop& prop = PropOf(chr);
    ModelPlayback& playback = chr.model->Playback();

    const float dx = target[0] - prop.position[0];
    const float dz = target[2] - prop.position[2];

    const float wanted = GameAtan2f(dx, dz);
    float position = ChrHeading(chr);

    bool arrived = false;
    float difference = wanted - position;
    if (wanted < position) {
        difference = difference + kTau;
    }

    float offset = difference;
    if (difference > kPi) {
        offset = kTau - difference;
    }

    float maxSpeed;
    float accel;
    if (mode == 2) {
        maxSpeed = kMaxSpeedA;
        accel = kAccelA;
    } else if (mode == 1) {
        if (offset < 0.3926991f) {
            maxSpeed = kMaxSpeedB1;
        } else if (offset < 1.2566371f) {
            maxSpeed = kMaxSpeedB2;
        } else {
            maxSpeed = kMaxSpeedB3;
        }
        accel = kAccelB;
    } else {
        if (offset < 0.3926991f) {
            maxSpeed = kMaxSpeedC1;
        } else if (offset < 1.2566371f) {
            maxSpeed = kMaxSpeedC2;
        } else {
            maxSpeed = kMaxSpeedC3;
        }
        accel = kAccelC;
    }

    maxSpeed *= playback.playSpeed;
    accel *= playback.playSpeed;

    ChrObjCallsApplySpeed(&position, wanted, speed, accel, accel * 2.0f, maxSpeed, scene_.chrs->Timing().clockTimer);

    if (position == wanted) {
        *speed = 0.0f;
        arrived = true;
    }

    playback.SetHeading(position);
    return arrived;
}

// -- Starting to move ---------------------------------------------------------------------

void ChrActions::StartMovingAnimation(Chr& chr, int speed) {
    // get_sound_at_range (play_hit_soundeffect_and_proper_volume passes the
    // speed the character goes at and whether its body is a man's).
    const Game::ChrRecords* records = scene_.chrs->Records();
    const Game::ChrRecord* body = records != nullptr ? records->At(chr.bodynum) : nullptr;
    const bool male = body != nullptr && body->isMale;

    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);
    bool twoHanded;
    bool flip;
    if ((left >= 0 && right >= 0) || (left < 0 && right < 0)) {
        twoHanded = false;
        flip = (RandomGetNext() & 1) != 0;
    } else if (OneHanded(left) || OneHanded(right)) {
        twoHanded = false;
        flip = left >= 0;
    } else {
        twoHanded = true;
        flip = left >= 0;
    }

    if (twoHanded) {
        Animate(chr, speed == 2 ? "sprinting" : speed == 1 ? "running" : "walking", flip, 0.0f, 0.5f, 16.0f);
        return;
    }

    if (male) {
        Animate(chr,
                speed == 2   ? "sprinting_one_handed_weapon"
                : speed == 1 ? "running_one_handed_weapon"
                             : "walking_unarmed",
                flip, 0.0f, 0.5f, 16.0f);
        return;
    }

    Animate(chr,
            speed == 2   ? "sprinting_one_handed_weapon"
            : speed == 1 ? "running_female"
                         : "walking_female",
            flip, 0.0f, 0.5f, 16.0f);
}

void ChrActions::StartWalkingAnimation(Chr& chr) {
    // chrlvWalkingAnimationRelated.
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);
    bool twoHanded;
    bool flip;
    if ((left >= 0 && right >= 0) || (left < 0 && right < 0)) {
        twoHanded = false;
        flip = (RandomGetNext() & 1) != 0;
    } else if (OneHanded(left) || OneHanded(right)) {
        twoHanded = false;
        flip = left >= 0;
    } else {
        twoHanded = true;
        flip = left >= 0;
    }

    if (twoHanded) {
        Animate(chr, "walking", flip, 0.0f, 0.5f, 16.0f);
    } else {
        const Strides& strides = StridesFor(chr);
        const float speed = (0.5f * strides.walk) / strides.walkUnarmed;
        Animate(chr, "walking_unarmed", flip, 0.0f, speed, 16.0f);
    }
}

void ChrActions::StartRunToPosition(Chr& chr, const Vec3& position) {
    // sub_GAME_7F024CF8.
    const Prop& prop = PropOf(chr);
    const float dx = prop.position[0] - position[0];
    const float dz = prop.position[2] - position[2];
    const float distance = std::sqrt((dx * dx) + (dz * dz));

    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);
    bool twoHanded = true;
    bool flip;
    if ((left >= 0 && right >= 0) || (left < 0 && right < 0)) {
        twoHanded = false;
        flip = (RandomGetNext() & 1) != 0;
    } else if (OneHanded(left) || OneHanded(right)) {
        twoHanded = false;
        flip = left >= 0;
    } else {
        flip = left >= 0;
    }

    StopFiring(chr);

    chr.actionType = ChrAction::RunPos;
    chr.actRunPos.position = position;
    chr.sleep = 0;
    chr.actRunPos.turnSpeed = 0.0f;
    chr.actRunPos.nearDistance = 30.0f;

    const Strides& strides = StridesFor(chr);
    if (twoHanded) {
        chr.actRunPos.eta60 = ToInt(distance / (strides.run * 0.5f));
        Animate(chr, "running", flip, 0.0f, 0.5f, 16.0f);
    } else {
        chr.actRunPos.eta60 = ToInt(distance / (strides.runOneHanded * 0.5f));
        Animate(chr, "running_one_handed_weapon", flip, 0.0f, 0.5f, 16.0f);
    }
}

bool ChrActions::PlotCourse(Chr& chr, const Vec3& position, int tile, Speed speed) {
    // plot_course_for_actor.
    const Prop& prop = PropOf(chr);
    const int mode = static_cast<int>(speed);
    const bool sameSpeed = chr.actionType == ChrAction::GoPos && chr.actGoPos.speedMode == static_cast<uint8_t>(mode);

    const int from = scene_.paths->NearestTo(*scene_.stan, prop.position, prop.stan);
    const int to = scene_.paths->NearestTo(*scene_.stan, position, tile);

    std::array<int, kMaxChrWaypoints> route;
    route.fill(-1);
    if (from < 0 || to < 0 || scene_.paths->FindRoute(from, to, route.data(), kMaxChrWaypoints) < 2) {
        return false;
    }

    StopFiring(chr);

    chr.actionType = ChrAction::GoPos;
    chr.actGoPos.targetPosition = position;
    chr.actGoPos.targetTile = tile;
    chr.actGoPos.targetWaypoint = to;
    chr.actGoPos.currentIndex = 0;
    chr.actGoPos.speedMode = static_cast<uint8_t>(mode);
    chr.actGoPos.speed = 0.0f;
    chr.actGoPos.way.age = static_cast<int32_t>(RandomGetNext() % 100u);
    chr.actGoPos.way.hasPosCopy = 0;
    chr.actGoPos.lastVisible60 = -1;

    chr.actGoPos.waypoints = route;

    SetGoPosTarget(chr);
    chr.sleep = 0;

    if (!sameSpeed) {
        StartMovingAnimation(chr, chr.actGoPos.speedMode);
    }

    const Step target = GoPosTarget(chr);
    if ((prop.flags & PropFlag::OnScreen) == 0 && OffScreenRoute(chr, target.position, target.tile)) {
        BeginMagic(chr, &chr.actGoPos.way, target.position);
    }
    return true;
}

void ChrActions::StartPatrol(Chr& chr, int pathIndex) {
    // set_actor_on_path.
    const PatrolPath& path = scene_.paths->Paths()[static_cast<size_t>(pathIndex)];
    const Prop& prop = PropOf(chr);
    int nextStep = -1;

    for (size_t count = 0; count < path.waypoints.size() && path.waypoints[count] >= 0; count++) {
        const Pad& pad = scene_.paths->PadOf(path.waypoints[count]);
        if (pad.stanTile != kNoTile && pad.stanTile == prop.stan) {
            const float dx = pad.position[0] - prop.position[0];
            const float dz = pad.position[2] - prop.position[2];
            if (PlaneLengthSquared(dx, dz) < 10000.0f) {
                nextStep = static_cast<int>(count);
                break;
            }
        }
    }
    if (nextStep < 0) {
        nextStep = 0;
    }

    StopFiring(chr);

    chr.actionType = ChrAction::Patrol;
    chr.actPatrol.path = pathIndex;
    chr.actPatrol.nextStep = nextStep;
    chr.actPatrol.forward = true;
    chr.actPatrol.way.age = static_cast<int32_t>(RandomGetNext() % 100u);
    chr.actPatrol.way.hasPosCopy = 0;
    chr.actPatrol.lastVisible60 = -1;
    chr.actPatrol.speed = 0.0f;

    SetPatrolTarget(chr);

    chr.sleep = 0;

    StartWalkingAnimation(chr);

    const Pad& next = PatrolPad(chr, 0);
    if ((prop.flags & PropFlag::OnScreen) == 0) {
        if (OffScreenRoute(chr, next.position, next.stanTile)) {
            BeginMagic(chr, &chr.actPatrol.way, next.position);
        }
    }
}

void ChrActions::BeginMagic(Chr& chr, Chr::WayData* way, const Vec3& target) {
    // chrlvSetGoposSegDistTotal.
    const Prop& prop = PropOf(chr);
    const float dx = target[0] - prop.position[0];
    const float dz = target[2] - prop.position[2];

    const float heading = GameAtan2f(dx, dz);

    way->mode = WayMode::Magic;
    way->segDistDone = 0.0f;
    way->segDistTotal = std::sqrt((dx * dx) + (dz * dz));

    chr.model->Playback().SetHeading(heading);
}

// -- Routes -------------------------------------------------------------------------------

int ChrActions::PatrolCalculateStep(const Chr& chr, bool* forward, int steps) const {
    // chrlvPatrolCalculateStep.
    const PatrolPath& path = scene_.paths->Paths()[static_cast<size_t>(chr.actPatrol.path)];
    // The path's waypoints end at a negative entry.
    const auto ended = [&path](int step) {
        return step < 0 || static_cast<size_t>(step) >= path.waypoints.size() ||
               path.waypoints[static_cast<size_t>(step)] < 0;
    };
    int nextStep = chr.actPatrol.nextStep;
    bool isForward = *forward;

    if (steps < 0) {
        isForward = !isForward;
        steps = -steps;
    }

    while (steps > 0) {
        steps--;

        if (isForward) {
            nextStep++;

            if (ended(nextStep)) {
                nextStep -= 2;

                if (path.isLoop) {
                    nextStep = 0;
                } else {
                    isForward = false;
                }
            }
        } else {
            nextStep--;

            if (nextStep < 0) {
                nextStep = 1;

                if (path.isLoop) {
                    nextStep = path.len - 1;
                } else {
                    isForward = true;
                }
            }
        }
    }

    *forward = isForward;
    return nextStep;
}

const Pad& ChrActions::PatrolPad(const Chr& chr, int steps) const {
    // chrlvGetPatrolStepPad.
    bool forward = chr.actPatrol.forward;
    const int step = PatrolCalculateStep(chr, &forward, steps);
    const PatrolPath& path = scene_.paths->Paths()[static_cast<size_t>(chr.actPatrol.path)];
    const size_t index = std::min(static_cast<size_t>(std::max(step, 0)), path.waypoints.size() - 1);
    return scene_.paths->PadOf(path.waypoints[index]);
}

void ChrActions::SetPatrolTarget(Chr& chr) {
    // chrlvSetNextActPatrolStepPadPos.
    const Pad& pad = PatrolPad(chr, 0);
    chr.actPatrol.way.mode = WayMode::Mode0;
    chr.actPatrol.way.modeTicks = 0;
    chr.actPatrol.way.hasPos = 0;
    chr.actPatrol.way.pos = pad.position;
}

void ChrActions::AdvancePatrolStep(Chr& chr) {
    // chrlvAdvancePatrolStep.
    chr.actPatrol.nextStep = PatrolCalculateStep(chr, &chr.actPatrol.forward, 1);
    SetPatrolTarget(chr);
}

void ChrActions::SetGoPosTarget(Chr& chr) {
    // chrlvActGoposSetTargetPosRelated.
    const Step target = GoPosTarget(chr);

    chr.actGoPos.way.mode = WayMode::Mode0;
    chr.actGoPos.way.modeTicks = 0;
    chr.actGoPos.way.hasPos = 0;
    chr.actGoPos.way.pos = target.position;

    chr.actGoPos.restartTicks = 0;
}

void ChrActions::AdvanceGoPos(Chr& chr) {
    // chrlvActGoposIncCurIndex.
    if (chr.actGoPos.currentIndex < 3) {
        chr.actGoPos.currentIndex++;
    } else {
        const int from = chr.actGoPos.waypoints[chr.actGoPos.currentIndex];
        chr.actGoPos.currentIndex = 1;
        if (from >= 0) {
            scene_.paths->FindRoute(from, chr.actGoPos.targetWaypoint, chr.actGoPos.waypoints.data(), kMaxChrWaypoints);
        }
    }

    SetGoPosTarget(chr);
}

int ChrActions::TicksToTarget(Chr& chr) {
    // chrlvMovementTargetRelated.
    const float speed = std::fabs(chr.model->Playback().speed);
    const Step target = GoPosTarget(chr);
    const Prop& prop = PropOf(chr);

    const float dx = std::fabs(target.position[0] - prop.position[0]);
    const float dz = std::fabs(target.position[2] - prop.position[2]);

    return ToInt((dx + dz) / (AnimationStride(chr) * speed));
}

void ChrActions::ConsiderReplotting(Chr& chr) {
    // chrlvPlotCourseRelated.
    if (chr.actGoPos.way.mode == WayMode::Magic) {
        return;
    }

    const int pending = chr.actGoPos.restartTicks;
    if (pending == 0) {
        int ticks = (TicksToTarget(chr) * 2) + 300;
        if (ticks >= 0x10000) {
            ticks = 0xFFFF;
        }
        chr.actGoPos.restartTicks = static_cast<uint16_t>(ticks);
        return;
    }

    const int elapsed = static_cast<uint16_t>(scene_.chrs->Timing().clockTimer);
    if (elapsed >= pending) {
        PlotCourse(chr, chr.actGoPos.targetPosition, chr.actGoPos.targetTile,
                   static_cast<Speed>(chr.actGoPos.speedMode));
        return;
    }
    chr.actGoPos.restartTicks = static_cast<uint16_t>(pending - elapsed);
}

// -- Route tests --------------------------------------------------------------------------

bool ChrActions::LineClear(Chr& chr, const Vec3& from, int fromTile, const Vec3& to, int toTile, int cdTypes) {
    // sub_GAME_7F030128.
    int tile = fromTile;
    bool pass = false;

    float width;
    float height;
    float always20;
    Chrs::WidthHeight(chr, &width, &height, &always20);

    ChrAi::SetMoving(chr, false);
    if (scene_.stan->TestLineUnobstructed(&tile, from[0], from[2], to[0], to[2], cdTypes, height, always20, 0.0f,
                                          1.0f) != 0 &&
        (toTile == kNoTile || tile == toTile)) {
        pass = true;
    }
    ChrAi::SetMoving(chr, true);
    return pass;
}

bool ChrActions::SidesClear(Chr& chr, const Vec3& from, int fromTile, const Vec3& to, float reach, int cdTypes) {
    // sub_GAME_7F0301FC: the character's width to either side of the line.
    Stan& stan = *scene_.stan;
    float width;
    float height;
    float always20;
    Chrs::WidthHeight(chr, &width, &height, &always20);

    float dx = to[0] - from[0];
    float dz = to[2] - from[2];
    if (dx == 0.0f && dz == 0.0f) {
        return true;
    }

    float norm = (dx * dx) + (dz * dz);
    norm = 1.0f / std::sqrt(norm);
    dx *= norm;
    dz *= norm;

    const float sideX = reach * dx;
    const float sideZ = reach * dz;
    bool clear = false;

    ChrAi::SetMoving(chr, false);

    int tile = fromTile;
    if (stan.TestLineUnobstructed(&tile, from[0], from[2], from[0] + sideZ, from[2] - sideX, cdTypes, height, always20,
                                  0.0f, 1.0f) != 0 &&
        stan.TestLineUnobstructed(&tile, from[0] + sideZ, from[2] - sideX, to[0] + sideZ, to[2] - sideX, cdTypes,
                                  height, always20, 0.0f, 1.0f) != 0) {
        tile = fromTile;
        if (stan.TestLineUnobstructed(&tile, from[0], from[2], from[0] - sideZ, from[2] + sideX, cdTypes, height,
                                      always20, 0.0f, 1.0f) != 0 &&
            stan.TestLineUnobstructed(&tile, from[0] - sideZ, from[2] + sideX, to[0] - sideZ, to[2] + sideX, cdTypes,
                                      height, always20, 0.0f, 1.0f) != 0) {
            clear = true;
        }
    }

    ChrAi::SetMoving(chr, true);
    return clear;
}

bool ChrActions::LineViaPoint(Chr& chr, const Vec3& from, int fromTile, const Vec3& via, const Vec3& to, int toTile,
                              int cdTypes) {
    // sub_GAME_7F0304AC.
    bool pass = false;
    float width;
    float height;
    float always20;
    Chrs::WidthHeight(chr, &width, &height, &always20);

    ChrAi::SetMoving(chr, false);

    int tile = fromTile;
    if (scene_.stan->TestLineUnobstructed(&tile, from[0], from[2], via[0], via[2], cdTypes, height, always20, 0.0f,
                                          1.0f) != 0) {
        int second = tile;
        if (scene_.stan->TestLineUnobstructed(&second, via[0], via[2], to[0], to[2], cdTypes, height, always20, 0.0f,
                                              1.0f) != 0 &&
            (toTile == kNoTile || second == toTile)) {
            pass = true;
        }
    }

    ChrAi::SetMoving(chr, true);
    return pass;
}

void ChrActions::OrderEdge(Vec3* a, Vec3* b, const Vec3& direction) {
    // chrlvSwapIfDiffArg2Determinate: puts the edge's point on the left of the
    // direction first.
    const Vec3 along = { (*b)[0] - (*a)[0], (*b)[1] - (*a)[1], (*b)[2] - (*a)[2] };
    const Vec3 left = { -direction[2], 0.0f, direction[0] };

    if (!(((left[0] * along[0]) + (left[2] * along[2])) > 0.0f)) {
        std::swap(*a, *b);
    }
}

bool ChrActions::FindEdges(Chr& chr, const Vec3& from, int fromTile, const Vec3& dest, Vec3* edgeA, Vec3* edgeB,
                           float extend, float radius, int cdTypes, bool nearest) {
    // sub_GAME_7F03081C (and sub_GAME_7F030D70 when `nearest`): whether the
    // character can walk from `from` to `dest`; when it cannot, the edges of
    // what is in the way, to steer round.
    Stan& stan = *scene_.stan;
    bool leftBlocked = false;
    bool rightBlocked = false;
    bool clear = false;
    Vec3 leftA{};
    Vec3 leftB{};
    Vec3 rightA{};
    Vec3 rightB{};
    float leftFraction = 0.0f;
    float rightFraction = 0.0f;

    float width;
    float height;
    float always20;
    Chrs::WidthHeight(chr, &width, &height, &always20);

    Vec3 direction = { dest[0] - from[0], 0.0f, dest[2] - from[2] };
    if (direction[0] == 0.0f && direction[2] == 0.0f) {
        return true;
    }

    float norm = (direction[0] * direction[0]) + (direction[2] * direction[2]);
    norm = 1.0f / std::sqrt(norm);
    direction[0] *= norm;
    direction[2] *= norm;

    const float insideX = 0.95f * (radius * direction[0]);
    const float insideZ = 0.95f * (radius * direction[2]);
    const float outsideX = 1.2f * (radius * direction[0]);
    const float outsideZ = 1.2f * (radius * direction[2]);

    ChrAi::SetMoving(chr, false);
    stan.ResetHits();

    int tile = fromTile;
    if (stan.TestLineUnobstructed(&tile, from[0], from[2], from[0] + insideZ, from[2] - insideX, cdTypes, height,
                                  always20, 0.0f, 1.0f) == 0 ||
        stan.TestLineUnobstructed(
            &tile, from[0] + insideZ, from[2] - insideX, (dest[0] + outsideZ) + (direction[0] * extend),
            (dest[2] - outsideX) + (direction[2] * extend), cdTypes, height, always20, 0.0f, 1.0f) == 0) {
        leftBlocked = true;

        stan.CollisionEdge(&leftA, &leftB);
        OrderEdge(&leftA, &leftB, direction);

        leftFraction = stan.LastCollision().fraction;
    }

    tile = fromTile;
    if (stan.TestLineUnobstructed(&tile, from[0], from[2], from[0] - insideZ, from[2] + insideX, cdTypes, height,
                                  always20, 0.0f, 1.0f) == 0 ||
        stan.TestLineUnobstructed(
            &tile, from[0] - insideZ, from[2] + insideX, (dest[0] - outsideZ) + (direction[0] * extend),
            (dest[2] + outsideX) + (direction[2] * extend), cdTypes, height, always20, 0.0f, 1.0f) == 0) {
        rightBlocked = true;

        stan.CollisionEdge(&rightA, &rightB);
        OrderEdge(&rightA, &rightB, direction);

        rightFraction = stan.LastCollision().fraction;
    }

    if (leftBlocked && rightBlocked) {
        if (nearest) {
            if (leftFraction < rightFraction) {
                *edgeA = leftA;
                *edgeB = leftB;
            } else {
                *edgeA = rightA;
                *edgeB = rightB;
            }
        } else {
            OrderEdge(&leftA, &rightA, direction);
            OrderEdge(&leftB, &rightB, direction);

            *edgeA = leftA;
            *edgeB = rightB;
        }
    } else if (leftBlocked) {
        *edgeA = leftA;
        *edgeB = leftB;
    } else if (rightBlocked) {
        *edgeA = rightA;
        *edgeB = rightB;
    } else {
        tile = fromTile;
        if (stan.TestLineUnobstructed(&tile, from[0], from[2], dest[0], dest[2], cdTypes, height, always20, 0.0f,
                                      1.0f) != 0 &&
            stan.TestVolume(&tile, dest[0], dest[2], radius, cdTypes, height, always20) < 0) {
            clear = true;
        } else {
            stan.CollisionEdge(edgeA, edgeB);
            OrderEdge(edgeA, edgeB, direction);
        }
    }

    ChrAi::SetMoving(chr, true);
    return clear;
}

bool ChrActions::FindDetour(Chr& chr, Vec3* base, bool baseIsFirst, Vec3* other, float reach, bool checkTarget,
                            const Vec3* target, Chr::WayData* way, float margin, int cdTypes, bool setCopy) {
    // sub_GAME_7F03130C: a point to steer by, `reach` units round the end of the
    // obstacle `base` is on. `other` is the obstacle's other end; the two
    // swap places in FindEdges' outputs with `baseIsFirst`.
    const Prop& prop = PropOf(chr);
    Vec3* edgeA = baseIsFirst ? base : other;
    Vec3* edgeB = baseIsFirst ? other : base;

    Vec3 toBase = { (*base)[0] - prop.position[0], 0.0f, (*base)[2] - prop.position[2] };
    const float norm = 1.0f / std::sqrt((toBase[0] * toBase[0]) + (toBase[2] * toBase[2]));
    toBase[0] *= reach * norm;
    toBase[2] *= reach * norm;

    float angle;
    if (reach * norm > 1.0f) {
        angle = DegToRad(45);
    } else {
        angle = GameAcosf(reach * norm);
    }
    if (!baseIsFirst && angle != 0.0f) {
        angle = kTau - angle;
    }

    const Vec3 offset = { (-std::cos(angle) * toBase[0]) + (std::sin(angle) * toBase[2]), 0.0f,
                          (-std::sin(angle) * toBase[0]) - (std::cos(angle) * toBase[2]) };
    const Vec3 point = { (*base)[0] + offset[0], (*base)[1], (*base)[2] + offset[2] };

    if (FindEdges(chr, prop.position, prop.stan, point, edgeA, edgeB, margin, chr.chrWidth, cdTypes, false) &&
        (!checkTarget || LineViaPoint(chr, prop.position, prop.stan, point, *target, kNoTile, cdTypes))) {
        if (setCopy) {
            way->hasPosCopy = 1;
            way->posCopy = point;
        } else {
            way->hasPos = 1;
            way->pos = point;
        }
        return true;
    }
    return false;
}

// -- Travelling -----------------------------------------------------------------------------

void ChrActions::TravelTick(Chr& chr, const Vec3& target, int targetTile, Chr::WayData* way) {
    // chrlvTravelTick: steers a character along a route, round what is in its
    // way, and opens the doors on it.
    (void)targetTile;
    const Prop& prop = PropOf(chr);
    const ChrTiming& timing = scene_.chrs->Timing();
    int cdTypes = CdType::Objs | CdType::Players | CdType::Chrs | CdType::PathBlocker | CdType::DoorsLockedToAi;
    if ((chr.hidden & ChrHidden::OffscreenPatrol) != 0) {
        cdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;
    }

    // The game runs this once (a loop of one pass); a break leaves it.
    do {
        if (way->mode == WayMode::Mode0 || way->mode == WayMode::Mode2) {
            const Vec3 point = target;

            if (FindEdges(chr, prop.position, prop.stan, point, &way->pos2, &way->pos3, -chr.chrWidth, chr.chrWidth,
                          CdType::PathBlocker, false)) {
                way->hasPos = 1;
                way->pos = point;
                way->mode = WayMode::Mode4;
            } else if (way->mode == WayMode::Mode0) {
                way->mode = WayMode::Mode1;
                way->modeTicks = 0;
            } else if (way->mode == WayMode::Mode2) {
                way->mode = WayMode::Mode3;
                way->modeTicks = 0;
            }
        } else if (way->mode == WayMode::Mode1) {
            const float reach = chr.chrWidth * 1.2f * 1.05f;
            Vec3 detour{};

            if (FindDetour(chr, &way->pos2, true, &detour, reach, true, &target, way, 0.0f, 0x10, false)) {
                way->mode = WayMode::Mode4;
            } else if (FindDetour(chr, &way->pos3, false, &detour, reach, true, &target, way, 0.0f, 0x10, false)) {
                way->mode = WayMode::Mode4;
            } else {
                way->modeTicks++;
                if (way->modeTicks >= kMaxWayMode) {
                    way->mode = WayMode::Mode2;
                }
            }
        } else if (way->mode == WayMode::Mode3) {
            const float reach = chr.chrWidth * 1.2f * 1.05f;
            Vec3 detour{};

            if (FindDetour(chr, &way->pos2, true, &detour, reach, false, nullptr, way, 0.0f, 0x10, false)) {
                way->mode = WayMode::Mode4;
            } else if (FindDetour(chr, &way->pos3, false, &detour, reach, false, nullptr, way, 0.0f, 0x10, false)) {
                way->mode = WayMode::Mode4;
            } else {
                way->modeTicks++;
                if (way->modeTicks >= kMaxWayMode) {
                    way->hasPos = 0;
                    way->hasPosCopy = way->hasPos;
                    way->pos = target;
                    way->posCopy = target;
                    way->mode = WayMode::Mode0;
                }
            }
        } else if (way->mode == WayMode::Mode4) {
            if (FindEdges(chr, prop.position, prop.stan, way->pos, &way->pos2, &way->pos3, -chr.chrWidth, chr.chrWidth,
                          cdTypes, true)) {
                way->hasPosCopy = 1;
                way->mode = WayMode::Mode0;
                way->posCopy = way->pos;
            } else {
                way->mode = WayMode::Mode5;
                way->modeTicks = 0;
            }
        } else if (way->mode == WayMode::Mode5) {
            const float reach = chr.chrWidth * 1.2f * 1.05f;
            Vec3 detour{};

            const auto angleBetween = [&prop](const Vec3& a, const Vec3& b) {
                // The unsigned angle between the directions to a and b, in [0, pi].
                float difference = GameAtan2f(a[0] - prop.position[0], a[2] - prop.position[2]) -
                                   GameAtan2f(b[0] - prop.position[0], b[2] - prop.position[2]);
                if (difference < 0.0f) {
                    difference += kTau;
                }
                if (difference >= kPi) {
                    difference -= kTau;
                }
                if (difference < 0.0f) {
                    difference = -difference;
                }
                return difference;
            };

            const float toPos2 = angleBetween(way->pos, way->pos2);
            const float toPos3 = angleBetween(way->pos, way->pos3);

            if (toPos2 < toPos3) {
                if (FindDetour(chr, &way->pos2, true, &detour, reach, false, nullptr, way, reach * 1.1f, cdTypes,
                               true)) {
                    way->mode = WayMode::Mode0;
                    break;
                }
                const float second = angleBetween(way->pos, way->pos2);
                const float third = angleBetween(way->pos, detour);
                if (third < second &&
                    FindDetour(chr, &detour, false, &detour, reach, false, nullptr, way, reach * 1.1f, cdTypes, true)) {
                    way->mode = WayMode::Mode0;
                    break;
                }
            } else {
                if (FindDetour(chr, &way->pos3, false, &detour, reach, false, nullptr, way, reach * 1.1f, cdTypes,
                               true)) {
                    way->mode = WayMode::Mode0;
                    break;
                }
                const float second = angleBetween(way->pos, way->pos3);
                const float third = angleBetween(way->pos, detour);
                if (third < second &&
                    FindDetour(chr, &detour, true, &detour, reach, false, nullptr, way, reach * 1.1f, cdTypes, true)) {
                    way->mode = WayMode::Mode0;
                    break;
                }
            }

            way->modeTicks++;
            if (way->modeTicks >= kMaxWayMode) {
                way->hasPosCopy = 0;
                way->mode = WayMode::Mode0;
            }
        }
    } while (false);

    if (way->hasPosCopy == 0) {
        way->posCopy = way->pos;
    }

    if ((way->age % 10) == 0) {
        // A door in the way, on the route to the next point, that characters may use.
        int doorProp = scene_.stan->FindPropOnLine(prop.stan, prop.position[0], prop.position[2], way->posCopy[0],
                                                   way->posCopy[2], CdType::ClosedDoors | CdType::AjarDoors);
        PlacedDoor* door = doorProp >= 0 ? scene_.doors->ForProp(doorProp) : nullptr;

        if (door != nullptr) {
            if ((door->flags2 & ObjectFlag2::AiCannotOperate) == 0) {
                const Vec3& doorPosition = (*scene_.props)[doorProp].position;
                const float dx = doorPosition[0] - prop.position[0];
                const float dy = doorPosition[1] - prop.position[1];
                const float dz = doorPosition[2] - prop.position[2];

                if (((dx * dx) + (dy * dy) + (dz * dz)) < 40000.0f) {
                    chr.actGoPos.restartTicks = 0;
                    const int doorIndex = static_cast<int>(door - scene_.doors->All().data());
                    scene_.doors->ChooseSwingDirection(doorIndex, prop.position);
                    scene_.doors->Activate(doorIndex, DoorState::Opening);

                    if ((chr.hidden & ChrHidden::OffscreenPatrol) == 0 && !IsPlaying(chr, "idle_unarmed") &&
                        !IsPlaying(chr, "idle")) {
                        ai_->StartIdle(chr, 16.0f);
                        chr.lastMoveOk60 = timing.globalTimer;
                    }
                } else {
                    door = nullptr;
                }
            } else {
                door = nullptr;
            }
        }

        if (door == nullptr || (chr.hidden & ChrHidden::OffscreenPatrol) != 0) {
            if (IsPlaying(chr, "idle_unarmed") || IsPlaying(chr, "idle")) {
                if (chr.actionType == ChrAction::Patrol) {
                    StartWalkingAnimation(chr);
                } else {
                    StartMovingAnimation(chr, chr.actGoPos.speedMode);
                }
            }

            if (door == nullptr) {
                chr.hidden &= ~ChrHidden::OffscreenPatrol;
            }
        }
    }

    ModelPlayback& playback = chr.model->Playback();
    if (chr.actionType == ChrAction::Patrol) {
        TurnTowards(chr, way->posCopy, 0, &chr.actPatrol.speed);
    } else {
        TurnTowards(chr, way->posCopy, chr.actGoPos.speedMode, &chr.actGoPos.speed);

        if (chr.actGoPos.speedMode == 2) {
            if (chr.actGoPos.speed != 0.0f) {
                playback.SetAnimSpeed(0.25f, 0.0f);
            } else if ((chr.chrflags & ChrFlag::IncreaseRunningSpeed) != 0) {
                playback.SetAnimSpeed(0.65f, 0.0f);
            } else {
                playback.SetAnimSpeed(0.5f, 0.0f);
            }
        } else if (chr.actGoPos.speedMode == 1) {
            if (chr.actGoPos.speed != 0.0f) {
                playback.SetAnimSpeed(0.4f, 0.0f);
            } else {
                playback.SetAnimSpeed(0.5f, 0.0f);
            }
        }
    }
}

void ChrActions::TickMagic(Chr& chr, Chr::WayData* way, float stride, const Vec3& target, int targetTile) {
    // chrlvTravelTickMagic: moves a character no one sees straight along its
    // route, by the distance its animation would have covered.
    const ChrTiming& timing = scene_.chrs->Timing();

    chr.invalidMove = 0;
    chr.lastMoveOk60 = timing.globalTimer;
    way->segDistDone += stride * std::fabs(chr.model->Playback().speed) * timing.globalTimerDelta;

    if (way->segDistTotal > way->segDistDone) {
        return;
    }

    ChrAi::SetMoving(chr, false);

    int tile = targetTile;
    if (scene_.stan->TestVolume(&tile, target[0], target[2], chr.chrWidth, kBodies, 0.0f, 1.0f) < 0 &&
        ClearOfObjects(target, tile)) {
        Prop& prop = PropOf(chr);
        prop.stan = tile;
        prop.position = target;
        chr.chrflags |= ChrFlag::Init;

        chr.model->Playback().SetPosition(ToPlayback(target));
        const int slot = SlotOf(chr);
        scene_.chrs->UpdateGround(slot, &prop.position, &prop.position, &chr.ground, timing);
        scene_.chrs->DetectRooms(slot);

        if (chr.actionType == ChrAction::Patrol) {
            AdvancePatrolStep(chr);
            BeginMagic(chr, way, PatrolPad(chr, 0).position);
        } else if (chr.actionType == ChrAction::GoPos) {
            const uint8_t index = chr.actGoPos.currentIndex;

            if (chr.actGoPos.waypoints[index] < 0) {
                if (index > 0) {
                    const Pad& pad = scene_.paths->PadOf(chr.actGoPos.waypoints[index - 1]);
                    chr.model->Playback().SetHeading(
                        GameAtan2f(prop.position[0] - pad.position[0], prop.position[2] - pad.position[2]));
                }

                StandUpFacingWalls(chr);
            } else {
                AdvanceGoPos(chr);
                BeginMagic(chr, way, GoPosTarget(chr).position);
            }
        }
    } else {
        way->segDistDone = way->segDistTotal;

        if (chr.actionType == ChrAction::Patrol) {
            chr.actPatrol.lastVisible60 = timing.globalTimer;
            SetPatrolTarget(chr);
        } else {
            chr.actGoPos.lastVisible60 = timing.globalTimer;
            SetGoPosTarget(chr);
        }
    }

    ChrAi::SetMoving(chr, true);
}

void ChrActions::TickPatrol(Chr& chr) {
    // chrlvTickPatrol.
    const ChrTiming& timing = scene_.chrs->Timing();
    const Prop& prop = PropOf(chr);
    Chr::ActPatrol& patrol = chr.actPatrol;

    const Pad* next = &PatrolPad(chr, 0);
    bool begunMagic = false;
    patrol.way.age += 1;
    chr.lastWalk60 = timing.globalTimer;

    if (patrol.way.mode != WayMode::Magic && (patrol.lastVisible60 + kDefaultTimer) < timing.globalTimer &&
        OffScreenRoute(chr, next->position, next->stanTile)) {
        begunMagic = true;
        BeginMagic(chr, &patrol.way, next->position);
    }

    if (patrol.way.mode == WayMode::Magic) {
        if (!begunMagic &&
            ((prop.flags & PropFlag::OnScreen) != 0 || !OffScreenRoute(chr, next->position, next->stanTile))) {
            patrol.lastVisible60 = timing.globalTimer;
            SetPatrolTarget(chr);
        } else {
            TickMagic(chr, &patrol.way, StridesFor(chr).walk, next->position, next->stanTile);
        }
    } else {
        if (ArrivingAt(chr.prevPosition, prop.position, next->position, kArrivalRange)) {
            AdvancePatrolStep(chr);
            next = &PatrolPad(chr, 0);
        }

        TravelTick(chr, next->position, next->stanTile, &patrol.way);
    }
}

void ChrActions::TickGoPos(Chr& chr) {
    // chrlvTickGoPos.
    const ChrTiming& timing = scene_.chrs->Timing();
    const Prop& prop = PropOf(chr);
    Chr::ActGoPos& gopos = chr.actGoPos;
    bool begunMagic = false;

    gopos.way.age += 1;
    chr.lastWalk60 = timing.globalTimer;

    if (chr.lastMoveOk60 < (timing.globalTimer - kLastMoveOkCheck)) {
        PlotCourse(chr, gopos.targetPosition, gopos.targetTile, static_cast<Speed>(gopos.speedMode));
    }

    ConsiderReplotting(chr);

    if (gopos.way.mode != WayMode::Magic && (gopos.lastVisible60 + kDefaultTimer) < timing.globalTimer) {
        const Step target = GoPosTarget(chr);
        if (OffScreenRoute(chr, target.position, target.tile)) {
            begunMagic = true;
            BeginMagic(chr, &gopos.way, target.position);
        }
    }

    if (ai_->State().seenBondRecentlyGuards >= kSeenBondLimit) {
        StandUpFacingWalls(chr);
        return;
    }

    if (gopos.way.mode == WayMode::Magic) {
        const Step target = GoPosTarget(chr);

        if (!begunMagic &&
            ((prop.flags & PropFlag::OnScreen) != 0 || !OffScreenRoute(chr, target.position, target.tile))) {
            SetGoPosTarget(chr);
            gopos.lastVisible60 = timing.globalTimer;
            return;
        }

        TickMagic(chr, &gopos.way, AnimationStride(chr), target.position, target.tile);
        return;
    }

    bool arrived = false;
    int waypoint = gopos.waypoints[gopos.currentIndex];
    if (waypoint >= 0) {
        if (ArrivingAt(chr.prevPosition, prop.position, scene_.paths->PadOf(waypoint).position, kArrivalRange)) {
            arrived = true;
        }
    } else if (ArrivingAt(chr.prevPosition, prop.position, gopos.targetPosition, kArrivalRange)) {
        StandUpFacingWalls(chr);
        return;
    }

    if (arrived) {
        AdvanceGoPos(chr);
    }

    // The point the route runs through `ahead` waypoints on, or its end.
    const auto routePoint = [&](int ahead) {
        const int next = gopos.waypoints[gopos.currentIndex + static_cast<size_t>(ahead)];
        if (next >= 0) {
            const Pad& pad = scene_.paths->PadOf(next);
            return Step{ pad.position, pad.stanTile };
        }
        return Step{ gopos.targetPosition, gopos.targetTile };
    };

    if ((gopos.way.age % 10) == 5) {
        if (gopos.waypoints[gopos.currentIndex] >= 0 && gopos.waypoints[gopos.currentIndex + 1u] >= 0) {
            const Step ahead = routePoint(2);
            if (LineClear(chr, prop.position, prop.stan, ahead.position, ahead.tile, CdType::PathBlocker) &&
                SidesClear(chr, prop.position, prop.stan, ahead.position, chr.chrWidth * 1.2f, CdType::PathBlocker)) {
                AdvanceGoPos(chr);
                AdvanceGoPos(chr);
            }
        }
    }

    if ((gopos.way.age % 10) == 0) {
        if (gopos.waypoints[gopos.currentIndex] >= 0) {
            const Step ahead = routePoint(1);
            if (LineClear(chr, prop.position, prop.stan, ahead.position, ahead.tile, CdType::PathBlocker) &&
                SidesClear(chr, prop.position, prop.stan, ahead.position, chr.chrWidth * 1.2f, CdType::PathBlocker)) {
                AdvanceGoPos(chr);
            }
        }
    }

    const Step current = GoPosTarget(chr);
    TravelTick(chr, current.position, current.tile, &gopos.way);
}

void ChrActions::TickRunPos(Chr& chr) {
    // chrlvTickRunPos.
    const ChrTiming& timing = scene_.chrs->Timing();
    const Prop& prop = PropOf(chr);
    ModelPlayback& playback = chr.model->Playback();
    chr.lastWalk60 = timing.globalTimer;

    if (chr.invalidMove == 1 || chr.lastMoveOk60 < (timing.globalTimer - kLastMoveOkCheck) ||
        ArrivingAt(chr.prevPosition, prop.position, chr.actRunPos.position, chr.actRunPos.nearDistance)) {
        const float frames = playback.anim != nullptr ? static_cast<float>(playback.anim->frameCount) : 0.0f;
        const float offset = 0.0f;

        float frame = playback.frame - offset;
        if (frame < 0.0f) {
            frame += frames;
        }

        if ((frames * 0.5f) < frame) {
            frame = frames - offset;
            playback.RampSpeedToFrame(frame, 16.0f);
        } else {
            frame = (frames * 0.5f) - offset;
            if (frame < 0) {
                frame += frames;
            }
            playback.RampSpeedToFrame(frame, 16.0f);
        }

        StandUpFacingWalls(chr);
        return;
    }

    TurnTowards(chr, chr.actRunPos.position, 1, &chr.actRunPos.turnSpeed);

    if (chr.actRunPos.eta60 > 0) {
        chr.actRunPos.eta60 -= timing.clockTimer;
    } else {
        const Strides& strides = StridesFor(chr);
        float stride = strides.run;

        if (IsPlaying(chr, "running_one_handed_weapon")) {
            stride = strides.runOneHanded;
        }

        chr.actRunPos.nearDistance += stride * timing.globalTimerDelta * std::fabs(playback.speed);
    }
}

} // namespace Frigate::Level
