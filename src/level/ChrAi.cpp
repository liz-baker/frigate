#include "level/ChrAi.h"

#include <algorithm>
#include <cmath>

#include "game/menu/CheatIds.h"
#include "level/Cheats.h"
#include "level/WeaponTable.h"
#include "model/Random.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.14159265f;
// CHRAI_TICKRATE_F.
constexpr float kTickRate = 60.0f;
// CHRLV_SEEN_RECENT_CHECK, CHRLV_10_SEC_TIMER and CHRLV_DEFAULT_TIMER.
constexpr int kSeenRecentCheck = 120;
constexpr int kTenSecondTimer = 600;
// chrCheckTargetInSight's sight: anything within 200 units in front, and the
// angles of its view cone and of the narrower cone it sees head on.
// DegToRad: the macro multiplies in the type of its argument, so an integer
// angle is worked in single precision and a floating literal in double.
constexpr float DegToRad(int degrees) {
    return static_cast<float>(degrees) * 6.2831855f / 360.0f;
}
constexpr float DegToRadDouble(double degrees) {
    return static_cast<float>((degrees * 6.2831855f) / 360.0f);
}
constexpr float kNearSightSquared = 200.0f * 200.0f;
constexpr float kSightHalfAngle = DegToRad(110);
constexpr float kSightFullArc = DegToRad(360 - 110);
constexpr float kFacingHalfAngle = DegToRad(45);
constexpr float kFacingFullArc = DegToRadDouble(360.0 - 45);
// AI_DOOR_STATE_*.
constexpr uint8_t kDoorStateClosed = 0x01;
constexpr uint8_t kDoorStateOpen = 0x02;
constexpr uint8_t kDoorStateClosing = 0x04;
constexpr uint8_t kDoorStateOpening = 0x08;
// QUADRANT_*.
constexpr uint8_t kQuadrantBack = 0x01;
constexpr uint8_t kQuadrantSide1 = 0x02;
constexpr uint8_t kQuadrantSide2 = 0x04;
constexpr uint8_t kQuadrant2ndWaypointToTarget = 0x10;
constexpr uint8_t kQuadrant20 = 0x20;
// ATTACKTYPE_DONTTURN.
constexpr int kAttackTypeDontTurn = 1 << 6;
// CAMERAMODE_INTRO and _SWIRL.
constexpr int kCameraModeIntro1 = 1;
constexpr int kCameraModeIntro2 = 2;
constexpr int kCameraModeSwirl = 3;
// GUNRIGHT and GUNLEFT.
constexpr int kGunRight = 0;
constexpr int kGunLeft = 1;

// ByteToRadian: the angle an AI command's byte names.
float ByteToRadian(uint8_t value) {
    return (static_cast<float>(value) * kTau) * (1.0f / 256.0f);
}

} // namespace

// The entity running a list: a character, or a vehicle or aircraft object.
struct ChrAi::Subject {
    Chr* chr = nullptr;
    PlacedObject* object = nullptr;
    bool aircraft = false;

    const AiList* List() const {
        return chr != nullptr ? chr->aiList : object->ai.list;
    }
    void Save(const AiList* list, size_t offset) const {
        if (chr != nullptr) {
            chr->aiList = list;
            chr->aiOffset = static_cast<uint16_t>(offset);
        } else {
            object->ai.list = list;
            object->ai.offset = static_cast<uint16_t>(offset);
        }
    }
    size_t Offset() const {
        return chr != nullptr ? chr->aiOffset : object->ai.offset;
    }
    void SetReturnList(int list) const {
        if (chr != nullptr) {
            chr->aiReturnList = static_cast<int16_t>(list);
        } else {
            object->ai.returnList = static_cast<int16_t>(list);
        }
    }
    int ReturnList() const {
        return chr != nullptr ? chr->aiReturnList : object->ai.returnList;
    }
};

// AiActions' defaults: a character that cannot act.
void AiActions::Tick(Chr&) {
}
void AiActions::Stop(Chr&) {
}
void AiActions::Kneel(Chr&) {
}
void AiActions::PointAtBond(Chr&) {
}
void AiActions::LookSurprised(Chr&) {
}
void AiActions::RaiseArms(Chr&) {
}
void AiActions::Surrender(Chr&) {
}
void AiActions::FadeOut(Chr&) {
}
void AiActions::StartAlarm(Chr&) {
}
void AiActions::PlayAnimation(Chr&, int, int, int, uint8_t, int) {
}
void AiActions::PlayAircraftAnimation(PlacedObject&, int, int, int, int) {
}
bool AiActions::Sidestep(Chr&) {
    return false;
}
bool AiActions::SideHop(Chr&) {
    return false;
}
bool AiActions::SideRun(Chr&) {
    return false;
}
bool AiActions::FiringWalk(Chr&) {
    return false;
}
bool AiActions::FiringRun(Chr&) {
    return false;
}
bool AiActions::FiringRoll(Chr&) {
    return false;
}
bool AiActions::AimAtTarget(Chr&, int, int) {
    return false;
}
bool AiActions::KneelAimAtTarget(Chr&, int, int) {
    return false;
}
bool AiActions::AimAtTargetUpdate(Chr&, int, int) {
    return false;
}
bool AiActions::FaceTarget(Chr&, int, int) {
    return false;
}
bool AiActions::ThrowGrenade(Chr&) {
    return false;
}
bool AiActions::PlotCourse(Chr&, const Vec3&, int, Speed) {
    return false;
}
void AiActions::StartPatrol(Chr&, int) {
}
bool AiActions::HitChr(Chr&, int, const Vec3&, int) {
    return false;
}
bool AiActions::HandleShot(Chr&, int, const Vec3&, int, bool) {
    return false;
}
bool AiActions::ExplosionDamage(Chr&, const Vec3&, float, bool) {
    return false;
}
void AiActions::FlinchRandomly(Chr&) {
}
void AiActions::UpdateAimProperties(Chr&) {
}
void AiActions::TriggerFireWeapon(Chr&) {
}
void AiActions::DropItems(Chr&) {
}
void AiActions::StopFiring(Chr&) {
}
void AiActions::StartIdleAnimation(Chr&, float, bool, bool) {
}

// -- Queries ----------------------------------------------------------------

namespace {

// ObjectRecord::flags of an object or door.
uint32_t& ObjectFlags(const LinkTarget& target) {
    return target.object != nullptr ? target.object->flags : target.door->flags;
}

} // namespace

Vec3 ChrAi::PlayerPosition() const {
    const AiPlayer player = host_.Player();
    return player.prop >= 0 ? (*scene_.props)[player.prop].position : Vec3{};
}

int ChrAi::PlayerTile() const {
    const AiPlayer player = host_.Player();
    return player.prop >= 0 ? (*scene_.props)[player.prop].stan : kNoTile;
}

const Pad* ChrAi::PadForId(int padId) const {
    return scene_.setup != nullptr ? scene_.setup->FindPad(padId) : nullptr;
}

bool ChrAi::IsDead(const Chr& chr) const {
    return chr.actionType == ChrAction::Die || chr.actionType == ChrAction::Dead;
}

bool ChrAi::IsNotDeadOrShot(const Chr& chr) const {
    const int action = chr.actionType;
    if (action == ChrAction::Die || action == ChrAction::Dead || action == ChrAction::PreArgh ||
        (action == ChrAction::Argh && (chr.chrflags & ChrFlag::Unnamed0x200) == 0)) {
        return false;
    }
    return true;
}

bool ChrAi::SawTargetRecently(const Chr& chr) const {
    return chr.lastSeeTarget60 > 0 && (scene_.chrs->Timing().globalTimer - chr.lastSeeTarget60) < kTenSecondTimer;
}

bool ChrAi::HeardTargetRecently(const Chr& chr) const {
    return chr.lastHearTarget60 > 0 && (scene_.chrs->Timing().globalTimer - chr.lastHearTarget60) < kTenSecondTimer;
}

bool ChrAi::IsHearingBond(const Chr& chr) const {
    return (chr.hidden & ChrHidden::AlertGuard) != 0;
}

void ChrAi::SetMoving(Chr& chr, bool unset) {
    if (unset) {
        chr.hidden &= ~ChrHidden::Moving;
    } else {
        chr.hidden |= ChrHidden::Moving;
    }
}

void ChrAi::SetTargetToPlayer(Chr& chr) {
    chr.lastSeeTarget60 = scene_.chrs->Timing().globalTimer;
    chr.lastKnownTargetPos = PlayerPosition();
    chr.targetTile = PlayerTile();
}

void ChrAi::AlertToPlayer(Chr& chr) {
    chr.hidden |= ChrHidden::AlertGuard;
    chr.lastHearTarget60 = scene_.chrs->Timing().globalTimer;
    chr.lastKnownTargetPos = PlayerPosition();
    chr.targetTile = PlayerTile();
}

float ChrAi::DistanceToBond(const Chr& chr) const {
    const Vec3 mine = chr.prop >= 0 ? (*scene_.props)[chr.prop].position : Vec3{};
    const Vec3 bond = PlayerPosition();
    const float dx = bond[0] - mine[0];
    const float dy = bond[1] - mine[1];
    const float dz = bond[2] - mine[2];
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

float ChrAi::AngleToBond(const Chr& chr) const {
    // get_distance_actor_to_position.
    const Vec3 mine = chr.prop >= 0 ? (*scene_.props)[chr.prop].position : Vec3{};
    const Vec3 bond = PlayerPosition();
    const float heading = ChrHeading(chr);
    const float between = GameAtan2f(bond[0] - mine[0], bond[2] - mine[2]);
    float angle = between - heading;
    if (between < heading) {
        angle += kTau;
    }
    return angle;
}

float ChrAi::AngleFromBond(const Chr& chr) const {
    const Vec3 mine = chr.prop >= 0 ? (*scene_.props)[chr.prop].position : Vec3{};
    const Vec3 bond = PlayerPosition();
    const float heading = host_.Player().yaw;
    const float between = GameAtan2f(mine[0] - bond[0], mine[2] - bond[2]);
    float angle = between - heading;
    if (between < heading) {
        angle += kTau;
    }
    return angle;
}

float ChrAi::DistanceToPad(const Chr& chr, int padId) const {
    const Vec3 mine = chr.prop >= 0 ? (*scene_.props)[chr.prop].position : Vec3{};
    const Pad* pad = PadForId(Chrs::ResolvePadId(chr, padId));
    if (pad == nullptr) {
        return 0.0f;
    }
    const float dx = pad->position[0] - mine[0];
    const float dy = pad->position[1] - mine[1];
    const float dz = pad->position[2] - mine[2];
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

bool ChrAi::PositionInSameRoom(Chr& chr, const Vec3& position, int tile) {
    if (chr.prop < 0) {
        return false;
    }
    const Prop& mine = (*scene_.props)[chr.prop];
    const float height = chr.chrHeight - 20.0f;
    bool pass = false;

    SetMoving(chr, false);
    int current = mine.stan;
    if (scene_.stan->TestLineUnobstructed(&current, mine.position[0], mine.position[2], position[0], position[2],
                                          CdType::Objs | CdType::Doors | CdType::PathBlocker | CdType::AiOpaque, height,
                                          height, 0.0f, 1.0f) != 0 &&
        current == tile) {
        pass = true;
    }
    SetMoving(chr, true);
    return pass;
}

bool ChrAi::CanSeeBond(Chr& chr) {
    bool pass = false;
    const AiPlayer player = host_.Player();
    if (player.visibleToGuards && chr.prop >= 0 && player.prop >= 0) {
        const Prop& mine = (*scene_.props)[chr.prop];
        const Prop& bond = (*scene_.props)[player.prop];
        const float height = chr.chrHeight - 20.0f;

        SetMoving(chr, false);
        host_.SetPlayerCollides(false);
        int current = mine.stan;
        if (scene_.stan->TestLineUnobstructed(
                &current, mine.position[0], mine.position[2], bond.position[0], bond.position[2],
                CdType::Objs | CdType::Doors | CdType::Chrs | CdType::PathBlocker | CdType::AiOpaque, height, height,
                0.0f, 1.0f) != 0 &&
            current == bond.stan) {
            chr.seenBondTime = scene_.chrs->Timing().globalTimer;
            pass = true;
        }
        SetMoving(chr, true);
        host_.SetPlayerCollides(true);
    }
    return pass;
}

int ChrAi::SpeedRatingInt(const Chr& chr, int scale) const {
    // chrlvGetGuard007SpeedRatingInt.
    int rating = chr.speedRating;
    rating = static_cast<int>(ReactionSpeed() * static_cast<float>(100 - rating)) + rating;
    return ((100 - rating) * scale) / 100;
}

float ChrAi::SpeedRating(const Chr& chr, float min, float max) const {
    // chrlvGetGuard007SpeedRating.
    float rating = static_cast<float>(chr.speedRating);
    rating = (ReactionSpeed() * (100.0f - rating)) + rating;
    return ((rating * (max - min)) / 100.0f) + min;
}

float ChrAi::ReactionSpeed() const {
    // get_007_reaction_speed.
    return host_.Ai007ReactionSpeed();
}

bool ChrAi::CheckTargetInSight(Chr& chr) {
    if (chr.prop < 0) {
        return false;
    }
    const Prop& mine = (*scene_.props)[chr.prop];
    const Vec3 bond = PlayerPosition();
    const float heading = ChrHeading(chr);
    // The game swaps x and z here.
    const float vecZ = bond[0] - mine.position[0];
    const float vecY = bond[1] - mine.position[1];
    const float vecX = bond[2] - mine.position[2];

    const float atn = GameAtan2f(vecZ, vecX);
    bool pass = false;
    float toFace = atn - heading;
    if (atn < heading) {
        toFace += kTau;
    }

    if (SawTargetRecently(chr)) {
        pass = true;
    } else {
        const float distanceSquared = (vecZ * vecZ) + (vecY * vecY) + (vecX * vecX);
        const bool inCone = toFace < kSightHalfAngle || toFace > kSightFullArc;
        if ((distanceSquared < (chr.visionRange * chr.visionRange * 100.0f * 100.0f) && inCone) ||
            (distanceSquared < kNearSightSquared && inCone)) {
            if (distanceSquared < host_.FarFogIntensitySquared()) {
                int distance = static_cast<int>((std::sqrt(distanceSquared) * 30.0f) / 16000.0f);

                // Not facing the player.
                if (toFace > kFacingHalfAngle && toFace < kFacingFullArc) {
                    float wrapped = toFace;
                    if (toFace > kPi) {
                        wrapped = kTau - toFace;
                    }
                    wrapped -= kFacingHalfAngle;
                    distance *= static_cast<int>((wrapped * 24.0f) / kTau) + 1;
                }

                distance = SpeedRatingInt(chr, distance) + 1;
                pass = (RandomGetNext() % static_cast<uint32_t>(distance)) == 0;
            }
        }
    }

    if (pass) {
        pass = CanSeeBond(chr);
    }
    if (pass) {
        SetTargetToPlayer(chr);
    }
    return pass;
}

bool ChrAi::IsTargetNearlyInSight(Chr& chr) {
    if (chr.prop < 0) {
        return false;
    }
    const AiPlayer player = host_.Player();
    if (player.prop < 0) {
        return false;
    }
    const Prop& mine = (*scene_.props)[chr.prop];
    const Prop& bond = (*scene_.props)[player.prop];
    int tile = mine.stan;

    scene_.stan->ResetHits();
    if (scene_.stan->WalkTiles(&tile, mine.position[0], mine.position[2], bond.position[0], bond.position[2])) {
        return false;
    }
    // The walk hit something: the player is nearly in sight if a path round
    // either end of the wall it hit is clear.
    Vec3 edgeA{};
    Vec3 edgeB{};
    scene_.stan->CollisionEdge(&edgeA, &edgeB);
    return LineViaPoint(chr, mine.position, mine.stan, edgeA, bond.position, bond.stan) ||
           LineViaPoint(chr, mine.position, mine.stan, edgeB, bond.position, bond.stan);
}

bool ChrAi::LineViaPoint(Chr& chr, const Vec3& from, int fromTile, const Vec3& via, const Vec3& to, int toTile) {
    // sub_GAME_7F0304AC with cdtypes 0: tiles only.
    float width;
    float height;
    float always20;
    Chrs::WidthHeight(chr, &width, &height, &always20);
    bool pass = false;

    SetMoving(chr, false);
    int current = fromTile;
    if (scene_.stan->TestLineUnobstructed(&current, from[0], from[2], via[0], via[2], 0, height, always20, 0.0f,
                                          1.0f) != 0) {
        int second = current;
        if (scene_.stan->TestLineUnobstructed(&second, via[0], via[2], to[0], to[2], 0, height, always20, 0.0f, 1.0f) !=
                0 &&
            (toTile == kNoTile || second == toTile)) {
            pass = true;
        }
    }
    SetMoving(chr, true);
    return pass;
}

bool ChrAi::HasStoppedOrPatrolling(const Chr& chr) const {
    if (chr.actionType == ChrAction::Stand && !chr.actStand.prestand && !chr.actStand.reaim) {
        return true;
    }
    if (chr.actionType == ChrAction::Anim) {
        if (chr.actAnim.playSfx || !chr.model) {
            return chr.actAnim.playSfx;
        }
        const ModelPlayback& playback = chr.model->Playback();
        return (playback.speed >= 0.0f && playback.frame >= playback.AnimEndFrame()) ||
               (playback.speed < 0.0f && playback.frame <= 0.0f);
    }
    return chr.actionType == ChrAction::Patrol;
}

float ChrAi::DistanceToChr(const Chr& chr, int id) {
    const Chr* other = scene_.chrs->FindById(chr, id);
    if (other == nullptr || !other->inUse || other->prop < 0 || chr.prop < 0) {
        return 0.0f;
    }
    const Vec3& mine = (*scene_.props)[chr.prop].position;
    const Vec3& theirs = (*scene_.props)[other->prop].position;
    const float dx = theirs[0] - mine[0];
    const float dy = theirs[1] - mine[1];
    const float dz = theirs[2] - mine[2];
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

float ChrAi::DistanceFromBondToPad(const Chr& chr, int padId) const {
    const Pad* pad = PadForId(Chrs::ResolvePadId(chr, padId));
    if (pad == nullptr) {
        return 0.0f;
    }
    const Vec3 bond = PlayerPosition();
    const float dx = pad->position[0] - bond[0];
    const float dy = pad->position[1] - bond[1];
    const float dz = pad->position[2] - bond[2];
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

bool ChrAi::PlayerCanSee(const Chr& chr) {
    // chrlvCurrentPlayerCall7F0B0E24: a clear line, past objects, doors and
    // path blockers, from the player to the character.
    const AiPlayer player = host_.Player();
    if (player.prop < 0 || chr.prop < 0) {
        return false;
    }
    const Prop& bond = (*scene_.props)[player.prop];
    const Prop& theirs = (*scene_.props)[chr.prop];
    int current = bond.stan;
    host_.SetPlayerCollides(false);
    const bool clear =
        scene_.stan->TestLineUnobstructed(&current, bond.position[0], bond.position[2], theirs.position[0],
                                          theirs.position[2], CdType::Objs | CdType::Doors | CdType::PathBlocker,
                                          bond.position[1], bond.position[1], 0.0f, 1.0f) != 0 &&
        current == theirs.stan;
    host_.SetPlayerCollides(true);
    return clear;
}

bool ChrAi::GoToBond(Chr& chr, Speed speed) {
    if (IsNotDeadOrShot(chr) && state_.seenBondRecentlyGuards < 10) {
        const AiPlayer player = host_.Player();
        if (player.prop >= 0) {
            const Prop& bond = (*scene_.props)[player.prop];
            return actions_.PlotCourse(chr, bond.position, bond.stan, speed);
        }
    }
    return false;
}

bool ChrAi::GoToChr(Chr& chr, int id, Speed speed) {
    if (IsNotDeadOrShot(chr) && state_.seenBondRecentlyGuards < 10) {
        const Chr* other = scene_.chrs->FindById(chr, id);
        if (other != nullptr && other->inUse && other->prop >= 0) {
            const Prop& theirs = (*scene_.props)[other->prop];
            return actions_.PlotCourse(chr, theirs.position, theirs.stan, speed);
        }
    }
    return false;
}

bool ChrAi::GoToPad(Chr& chr, int padId, Speed speed) {
    if (padId < 0 || !IsNotDeadOrShot(chr) || state_.seenBondRecentlyGuards >= 10) {
        return false;
    }
    const Pad* pad = PadForId(Chrs::ResolvePadId(chr, padId));
    if (pad == nullptr || pad->stanTile == kNoTile) {
        return false;
    }
    // A pad that is not vertical is approached from the side it faces.
    if (pad->up[1] < 0.5f) {
        int tile = pad->stanTile;
        const float reach = chr.chrWidth * 1.1f;
        const Vec3 region = { (pad->up[0] * reach) + pad->position[0], (pad->up[1] * reach) + pad->position[1],
                              (pad->up[2] * reach) + pad->position[2] };
        return scene_.stan->WalkTiles(&tile, pad->position[0], pad->position[2], region[0], region[2]) &&
               actions_.PlotCourse(chr, region, tile, speed);
    }
    return actions_.PlotCourse(chr, pad->position, pad->stanTile, speed);
}

void ChrAi::GuardsHeardSound(float noise) {
    for (Chr& chr : scene_.chrs->Slots()) {
        if (chr.inUse && DistanceToBond(chr) < chr.hearingScale * (noise * 100.0f)) {
            AlertToPlayer(chr);
        }
    }
}

// -- Waypoint queries the commands make --------------------------------------

int ChrAi::PathQuadrantPad(const Vec3& position, int tile, float rotation, uint8_t quadrant) {
    // chrlvFindPathNeighborRelated.
    const int waypoint = scene_.paths->NearestTo(*scene_.stan, position, tile);
    if (waypoint < 0) {
        return -1;
    }
    switch (quadrant) {
        case kQuadrantBack:
            rotation += DegToRad(180);
            break;
        case kQuadrantSide1:
            rotation += DegToRad(90);
            break;
        case kQuadrantSide2:
            rotation += DegToRad(270);
            break;
        default:
            break;
    }
    if (rotation >= kTau) {
        rotation -= kTau;
    }

    // sub_GAME_7F033780: the waypoint is within 90 degrees of `rotation`.
    const auto within = [&](int candidate) {
        const Pad& pad = scene_.paths->PadOf(candidate);
        const float dx = pad.position[0] - position[0];
        const float dz = pad.position[2] - position[2];
        const float heading = GameAtan2f(dx, dz);
        float difference = rotation - heading;
        if (rotation < heading) {
            difference += kTau;
        }
        return difference < DegToRad(90) || difference > DegToRad(270);
    };
    if (within(waypoint)) {
        return scene_.paths->All()[static_cast<size_t>(waypoint)].padId;
    }
    for (const int32_t neighbour : scene_.paths->All()[static_cast<size_t>(waypoint)].neighbours) {
        if (within(neighbour)) {
            return scene_.paths->All()[static_cast<size_t>(neighbour)].padId;
        }
    }
    return -1;
}

bool ChrAi::SetPadPresetByQuadrant(Chr& chr, uint8_t quadrant) {
    // check_2328_preset_set_with_method.
    if (chr.prop < 0) {
        return false;
    }
    const Prop& mine = (*scene_.props)[chr.prop];
    if (quadrant == kQuadrant2ndWaypointToTarget || quadrant == kQuadrant20) {
        const AiPlayer player = host_.Player();
        if (player.prop < 0) {
            return false;
        }
        const Prop& bond = (*scene_.props)[player.prop];
        const int mine2 = scene_.paths->NearestTo(*scene_.stan, mine.position, mine.stan);
        const int theirs = scene_.paths->NearestTo(*scene_.stan, bond.position, bond.stan);
        if (mine2 >= 0 && theirs >= 0) {
            if (quadrant == kQuadrant2ndWaypointToTarget) {
                std::array<int, Waypoints::kRouteLimit> route{};
                if (scene_.paths->FindRoute(mine2, theirs, route.data(), Waypoints::kRouteLimit) >=
                    Waypoints::kRouteLimit) {
                    chr.padPreset1 = static_cast<int16_t>(scene_.paths->All()[static_cast<size_t>(route[1])].padId);
                    return true;
                }
            } else {
                const int next = scene_.paths->FindNextStepToward(mine2, theirs);
                if (next >= 0) {
                    chr.padPreset1 = static_cast<int16_t>(scene_.paths->All()[static_cast<size_t>(next)].padId);
                    return true;
                }
            }
        }
    } else {
        const int pad = PathQuadrantPad(mine.position, mine.stan, ChrHeading(chr), quadrant);
        if (pad >= 0) {
            chr.padPreset1 = static_cast<int16_t>(pad);
            return true;
        }
    }
    return false;
}

bool ChrAi::SetPresetToPlayerNeighbour(Chr& chr, uint8_t padnum) {
    // sub_GAME_7F033AAC.
    if (padnum == 16 || padnum == 32) {
        return SetPadPresetByQuadrant(chr, padnum);
    }
    const AiPlayer player = host_.Player();
    if (player.prop < 0) {
        return false;
    }
    const Prop& bond = (*scene_.props)[player.prop];
    const int pad = PathQuadrantPad(bond.position, bond.stan, player.yaw, padnum);
    if (pad >= 0) {
        chr.padPreset1 = static_cast<int16_t>(pad);
        return true;
    }
    return false;
}

bool ChrAi::SetChrPresetNear(Chr& chr, float distance) {
    // sub_GAME_7F033B38.
    if (chr.prop < 0) {
        return false;
    }
    const Prop& mine = (*scene_.props)[chr.prop];
    const int room = scene_.stan->TileRoom(mine.stan);
    const Vec3 lower = { mine.position[0] - distance, mine.position[1] - distance, mine.position[2] - distance };
    const Vec3 upper = { mine.position[0] + distance, mine.position[1] + distance, mine.position[2] + distance };
    for (Chr& other : scene_.chrs->Slots()) {
        if (&other == &chr || !other.inUse || IsDead(other) || other.prop < 0) {
            continue;
        }
        const Prop& theirs = (*scene_.props)[other.prop];
        const Vec3& pos = theirs.position;
        if (pos[0] >= lower[0] && pos[0] <= upper[0] && pos[1] >= lower[1] && pos[1] <= upper[1] &&
            pos[2] >= lower[2] && pos[2] <= upper[2]) {
            const int theirRoom = scene_.stan->TileRoom(theirs.stan);
            if (theirRoom == room || host_.RoomsSharePortal(room, theirRoom)) {
                chr.chrPreset1 = other.chrnum;
                return true;
            }
        }
    }
    return false;
}

// -- Spawning -----------------------------------------------------------------

bool ChrAi::SpawnAtCoord(int body, int head, const Vec3& position, int tile, float angle, const AiList* list,
                         uint32_t flags, Chr** spawned) {
    const int slot =
        scene_.chrs->SpawnAtCoord(body, head, position, tile, angle, list, flags, scene_.chrs->Timing(), RandomGetNext,
                                  [this](const Vec3& at, int atTile) { return host_.PositionOffScreen(at, atTile); });
    if (slot < 0) {
        return false;
    }
    *spawned = &(*scene_.chrs)[slot];
    return true;
}

bool ChrAi::SpawnAtPad(const Chr& self, int body, int head, int pad, const AiList* list, uint32_t flags,
                       Chr** spawned) {
    const Pad* record = PadForId(Chrs::ResolvePadId(self, pad));
    if (record == nullptr) {
        return false;
    }
    return SpawnAtCoord(body, head, record->position, record->stanTile, GameAtan2f(record->look[0], record->look[2]),
                        list, flags, spawned);
}

bool ChrAi::SpawnAtChr(Chr& self, int body, int head, int chrnum, const AiList* list, uint32_t flags, Chr** spawned) {
    Chr* chr = scene_.chrs->FindById(self, chrnum);
    if (chr == nullptr || chr->prop < 0 || (chr->chrflags & ChrFlag::HasBeenOnScreen) != 0) {
        return false;
    }
    const Prop& prop = (*scene_.props)[chr->prop];
    return SpawnAtCoord(body, head, prop.position, prop.stan, ChrHeading(*chr), list, flags, spawned);
}

bool ChrAi::TeleportChrToPad(Chr& self, Chr& target, int padId) {
    const Pad* pad = PadForId(Chrs::ResolvePadId(self, padId));
    if (pad == nullptr || target.prop < 0) {
        return false;
    }
    const float facing = GameAtan2f(pad->look[0], pad->look[2]);
    Vec3 position = pad->position;
    int tile = pad->stanTile;

    SetMoving(target, false);
    bool pass = false;
    if (scene_.chrs->AdjustPosForSpawn(&position, &tile, facing, true, [](const Vec3&, int) { return true; })) {
        Prop& prop = (*scene_.props)[target.prop];
        prop.position = position;
        prop.stan = tile;
        target.chrflags |= ChrFlag::Init;
        if (target.model) {
            target.model->Playback().SetHeading(facing);
            target.model->Playback().SetPosition({ position[0], position[1], position[2] });
        }
        scene_.chrs->DetectRooms(static_cast<int>(&target - scene_.chrs->Slots().data()));
        if (target.prop == host_.Player().prop) {
            host_.SetPlayerCollisionPosition(position, tile);
        }
        pass = true;
    }
    SetMoving(target, true);
    return pass;
}

bool ChrAi::Clone(Chr& self, const AiCommand& command) {
    const uint16_t listId = command.U16(AiTRYCloningChrRecord::AI_LIST_ID);
    const AiList* list = scene_.lists->FindById(listId);
    Chr* chr = scene_.chrs->FindById(self, command.U8(AiTRYCloningChrRecord::CHR_NUM));
    if (chr == nullptr || (chr->chrflags & ChrFlag::Clone) == 0) {
        return false;
    }
    Chr* clone = nullptr;
    if (!SpawnAtChr(self, chr->bodynum, -1, chr->chrnum, list, 0, &clone)) {
        return false;
    }
    const int chrnum = chr->chrnum + 10000;
    if (scene_.chrs->FindById(self, chrnum) == nullptr) {
        clone->chrnum = static_cast<int16_t>(chrnum);
    }
    host_.CloneChr(*chr, *clone);
    return true;
}

bool ChrAi::StartAlarmAt(Chr& chr, int pad) {
    // chrTryStartAlarm: the alarm must be an object at the pad that is still
    // whole.
    pad = Chrs::ResolvePadId(chr, pad);
    if (!IsNotDeadOrShot(chr)) {
        return false;
    }
    // scan_position_data_table_for_normal_object_at_preset: the newest
    // ordinary object placed on the pad.
    const auto& active = scene_.props->Active();
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        if ((*scene_.props)[*it].type != PropType::Obj) {
            continue;
        }
        const PlacedObject* object = scene_.objects->ForProp(*it);
        if (object != nullptr && object->def != kNoDef &&
            static_cast<int16_t>(pad) == scene_.setup->PropDefs()[object->def].pad) {
            if (Objects::IsHealthy(*object)) {
                actions_.StartAlarm(chr);
                return true;
            }
            return false;
        }
    }
    return false;
}

// -- chrlvActionTick ------------------------------------------------------------

void ChrAi::MergeKneelToStand(Chr& chr, float mergeTime) {
    actions_.StopFiring(chr);
    chr.actionType = ChrAction::Stand;
    chr.actStand.prestand = 0;
    chr.actStand.faceEntityType = 0;
    chr.actStand.faceEntityId = 0;
    chr.actStand.reaim = 0;
    chr.actStand.turning = 2;
    chr.actStand.checkFacingWall = 0;
    chr.actStand.wallCount = static_cast<int32_t>(RandomGetNext() % 120u) + 180;

    float sleep = mergeTime;
    if (chr.model && chr.model->Playback().playSpeed != 1.0f) {
        sleep = mergeTime / chr.model->Playback().playSpeed;
    }
    if (sleep > 127.0f) {
        sleep = 127.0f;
    }
    chr.sleep = static_cast<int8_t>(static_cast<int32_t>(sleep));

    StartIdle(chr, mergeTime);
}

void ChrAi::StartIdle(Chr& chr, float mergeTime) {
    // chrlvIdleAnimationRelated.
    const int left = chr.weaponsHeld[kGunLeft];
    const int right = chr.weaponsHeld[kGunRight];
    const auto oneHanded = [&](int prop) {
        const PlacedObject* weapon = prop >= 0 ? scene_.objects->ForProp(prop) : nullptr;
        return weapon != nullptr && host_.WeaponHasFlag(weapon->weaponNum, WeaponFlag::OnlyOneHanded);
    };
    if ((left >= 0 && right >= 0) || (left < 0 && right < 0) || oneHanded(left) || oneHanded(right)) {
        const bool mirror = (RandomGetNext() & 1) != 0;
        actions_.StartIdleAnimation(chr, mergeTime, true, mirror);
    } else if (right >= 0 || left >= 0) {
        actions_.StartIdleAnimation(chr, mergeTime, false, left >= 0);
    }
}

void ChrAi::ActionTick(Chr& chr) {
    const ChrTiming& timing = scene_.chrs->Timing();
    if (timing.clockTimer <= 0) {
        return;
    }

    if (chr.actionType == ChrAction::Init) {
        chr.chrflags |= ChrFlag::Init;
        MergeKneelToStand(chr, 0.0f);
        chr.sleep = 0;
    }

    if ((chr.hidden & ChrHidden::TimerActive) != 0) {
        chr.timer60 += timing.clockTimer;
    }

    // sleep is an s8.
    chr.sleep = static_cast<int8_t>(chr.sleep - timing.clockTimer);

    if (static_cast<int32_t>(chr.sleep) < 0 || (chr.chrflags & ChrFlag::Unnamed0x40000) != 0) {
        chr.sleep = 0;
        Run(chr);
        actions_.Tick(chr);

        chr.chrflags &= ~ChrFlag::NearMiss;
        chr.hidden &= ~(ChrHidden::AlertGuard | ChrHidden::BackgroundAi);
        chr.chrSeeShot = kChrFree;
        chr.chrSeeDie = kChrFree;
    }
}

void ChrAi::AllChrTick() {
    for (Chr& chr : scene_.chrs->Background()) {
        ActionTick(chr);
    }

    state_.seenBondRecentlyGuards = 0;
    const int now = scene_.chrs->Timing().globalTimer;
    for (const Chr& chr : scene_.chrs->Slots()) {
        if (chr.inUse && chr.lastSeeTarget60 > 0 && now - chr.lastSeeTarget60 < kSeenRecentCheck) {
            state_.seenBondRecentlyGuards++;
        }
    }
}

// -- The interpreter --------------------------------------------------------------

void ChrAi::Run(Chr& chr) {
    Subject subject;
    subject.chr = &chr;
    Execute(subject);
}

void ChrAi::RunObject(PlacedObject& object) {
    Subject subject;
    subject.object = &object;
    subject.aircraft = object.type == static_cast<uint8_t>(PropDefType::Aircraft);
    Execute(subject);
}

void ChrAi::Execute(Subject& subject) {
    // The commands that act on the running character see a scratch one when a
    // vehicle or aircraft runs the list (the game would read the object's
    // fields as a character's).
    Chr scratch;
    Chr& self = subject.chr != nullptr ? *subject.chr : scratch;

    const AiList* list = subject.List();
    if (list == nullptr) {
        return;
    }
    size_t offset = subject.Offset();

    const auto targetOf = [&](int tag) { return scene_.links->FindByTag(static_cast<uint16_t>(tag)); };
    const auto chrOf = [&](int id) { return scene_.chrs->FindById(self, id); };
    const ChrTiming& timing = scene_.chrs->Timing();

    for (;;) {
        const uint8_t* bytes = list->bytes.data();
        const size_t size = list->bytes.size();
        if (offset >= size) {
            // A list with no end command; the game reads past it.
            return;
        }
        const AiCommand command(bytes, size, offset);

        // The jump every conditional command ends in: to the command's label
        // when the test holds, otherwise past the command.
        const auto jump = [&](bool taken, size_t commandSize, size_t labelField) {
            if (taken) {
                offset = AiGoToLabel(bytes, size, offset, command.U8(labelField));
            } else {
                offset += commandSize;
            }
        };

        switch (command.Cmd()) {
            case AI_GotoNext:
                offset = AiGoToLabel(bytes, size, offset, command.U8(AiGotoNextRecord::GOTOLABEL));
                break;
            case AI_GotoFirst:
                offset = AiGoToLabel(bytes, size, 0, command.U8(AiGotoFirstRecord::GOTOLABEL));
                break;
            case AI_Label:
                offset += AiLabelRecord::kSize;
                break;
            case AI_Yield:
                offset += AiYieldRecord::kSize;
                subject.Save(list, offset);
                return;
            case AI_EndList:
                // Not an error: the same as a yield that leaves the offset
                // where it was.
                return;
            case AI_SetChrAiList: {
                const uint16_t listId = command.U16(AiSetChrAiListRecord::AI_LIST_ID);
                const uint8_t chrNum = command.U8(AiSetChrAiListRecord::CHR_NUM);
                if (chrNum == static_cast<uint8_t>(kChrSelf)) {
                    list = scene_.lists->FindById(listId);
                    if (list == nullptr) {
                        return;
                    }
                    offset = 0;
                } else {
                    if (Chr* chr = chrOf(chrNum)) {
                        chr->aiList = scene_.lists->FindById(listId);
                        chr->aiOffset = 0;
                        chr->sleep = 0;
                    }
                    offset += AiSetChrAiListRecord::kSize;
                }
                break;
            }
            case AI_SetReturnAiList:
                subject.SetReturnList(command.U16(AiSetReturnAiListRecord::AI_LIST_ID));
                offset += AiSetReturnAiListRecord::kSize;
                break;
            case AI_Return:
                list = scene_.lists->FindById(subject.ReturnList());
                if (list == nullptr) {
                    return;
                }
                offset = 0;
                break;
            case AI_Stop:
                if (IsNotDeadOrShot(self)) {
                    actions_.Stop(self);
                }
                offset += AiStopRecord::kSize;
                break;
            case AI_Kneel:
                if (IsNotDeadOrShot(self)) {
                    actions_.Kneel(self);
                }
                offset += AiKneelRecord::kSize;
                break;
            case AI_PlayAnimation: {
                const int animation = command.U16(AiPlayAnimationRecord::ANIMATION_ID);
                int startFrame = command.U16(AiPlayAnimationRecord::START_TIME30);
                int endFrame = command.U16(AiPlayAnimationRecord::END_TIME30);
                if (startFrame == 0xFFFF) {
                    startFrame = 0;
                }
                if (endFrame == 0xFFFF) {
                    endFrame = -1;
                }
                if (subject.chr != nullptr) {
                    if (IsNotDeadOrShot(self)) {
                        actions_.PlayAnimation(self, animation, startFrame, endFrame,
                                               command.U8(AiPlayAnimationRecord::BITFIELD),
                                               command.U8(AiPlayAnimationRecord::INTERPOL_TIME60));
                    }
                } else if (subject.aircraft) {
                    actions_.PlayAircraftAnimation(*subject.object, animation, startFrame, endFrame,
                                                   command.U8(AiPlayAnimationRecord::INTERPOL_TIME60));
                }
                offset += AiPlayAnimationRecord::kSize;
                break;
            }
            case AI_IFPlayingAnimation:
                jump(self.actionType == ChrAction::Anim, AiIFPlayingAnimationRecord::kSize,
                     AiIFPlayingAnimationRecord::GOTOLABEL);
                break;
            case AI_PointAtBond:
                if (IsNotDeadOrShot(self)) {
                    actions_.PointAtBond(self);
                }
                offset += AiPointAtBondRecord::kSize;
                break;
            case AI_LookSurprised:
                if (IsNotDeadOrShot(self)) {
                    actions_.LookSurprised(self);
                }
                offset += AiLookSurprisedRecord::kSize;
                break;
            case AI_IFImOnPatrolOrStopped:
                jump(HasStoppedOrPatrolling(self), AiIFImOnPatrolOrStoppedRecord::kSize,
                     AiIFImOnPatrolOrStoppedRecord::GOTOLABEL);
                break;
            case AI_IFChrDyingOrDead: {
                const Chr* chr = chrOf(command.U8(AiIFChrDyingOrDeadRecord::CHR_NUM));
                jump(chr == nullptr || IsDead(*chr), AiIFChrDyingOrDeadRecord::kSize,
                     AiIFChrDyingOrDeadRecord::GOTOLABEL);
                break;
            }
            case AI_IFChrDoesNotExist: {
                const Chr* chr = chrOf(command.U8(AiIFChrDoesNotExistRecord::CHR_NUM));
                jump(chr == nullptr || !chr->inUse, AiIFChrDoesNotExistRecord::kSize,
                     AiIFChrDoesNotExistRecord::GOTOLABEL);
                break;
            }
            case AI_IFISeeBond:
                jump(CheckTargetInSight(self), AiIFISeeBondRecord::kSize, AiIFISeeBondRecord::GOTOLABEL);
                break;
            case AI_TRYSidestepping:
                jump(actions_.Sidestep(self), AiTRYSidesteppingRecord::kSize, AiTRYSidesteppingRecord::GOTOLABEL);
                break;
            case AI_TRYSideHopping:
                jump(actions_.SideHop(self), AiTRYSideHoppingRecord::kSize, AiTRYSideHoppingRecord::GOTOLABEL);
                break;
            case AI_TRYSideRunning:
                jump(actions_.SideRun(self), AiTRYSideRunningRecord::kSize, AiTRYSideRunningRecord::GOTOLABEL);
                break;
            case AI_TRYFiringWalk:
                jump(actions_.FiringWalk(self), AiTRYFiringWalkRecord::kSize, AiTRYFiringWalkRecord::GOTOLABEL);
                break;
            case AI_TRYFiringRun:
                jump(actions_.FiringRun(self), AiTRYFiringRunRecord::kSize, AiTRYFiringRunRecord::GOTOLABEL);
                break;
            case AI_TRYFiringRoll:
                jump(actions_.FiringRoll(self), AiTRYFiringRollRecord::kSize, AiTRYFiringRollRecord::GOTOLABEL);
                break;
            case AI_TRYFireOrAimAtTarget:
                jump(actions_.AimAtTarget(self, command.U16(AiTRYFireOrAimAtTargetRecord::BITFIELD),
                                          command.U16(AiTRYFireOrAimAtTargetRecord::TARGET)),
                     AiTRYFireOrAimAtTargetRecord::kSize, AiTRYFireOrAimAtTargetRecord::GOTOLABEL);
                break;
            case AI_TRYFireOrAimAtTargetKneel:
                jump(actions_.KneelAimAtTarget(self, command.U16(AiTRYFireOrAimAtTargetKneelRecord::BITFIELD),
                                               command.U16(AiTRYFireOrAimAtTargetKneelRecord::TARGET)),
                     AiTRYFireOrAimAtTargetKneelRecord::kSize, AiTRYFireOrAimAtTargetKneelRecord::GOTOLABEL);
                break;
            case AI_IFImFiringAndLockedForward:
                jump(self.actionType == ChrAction::Attack && !self.actAttack.typeOfMotion &&
                         (self.actAttack.attackType & kAttackTypeDontTurn) != 0,
                     AiIFImFiringAndLockedForwardRecord::kSize, AiIFImFiringAndLockedForwardRecord::GOTOLABEL);
                break;
            case AI_IFImFiring:
                jump(self.actionType == ChrAction::Attack, AiIFImFiringRecord::kSize, AiIFImFiringRecord::GOTOLABEL);
                break;
            case AI_TRYFireOrAimAtTargetUpdate:
                jump(actions_.AimAtTargetUpdate(self, command.U16(AiTRYFireOrAimAtTargetUpdateRecord::BITFIELD),
                                                command.U16(AiTRYFireOrAimAtTargetUpdateRecord::TARGET)),
                     AiTRYFireOrAimAtTargetUpdateRecord::kSize, AiTRYFireOrAimAtTargetUpdateRecord::GOTOLABEL);
                break;
            case AI_TRYFacingTarget:
                jump(actions_.FaceTarget(self, command.U16(AiTRYFacingTargetRecord::BITFIELD),
                                         command.U16(AiTRYFacingTargetRecord::TARGET)),
                     AiTRYFacingTargetRecord::kSize, AiTRYFacingTargetRecord::GOTOLABEL);
                break;
            case AI_HitChrWithItem: {
                Chr* chr = chrOf(command.U8(AiHitChrWithItemRecord::CHR_NUM));
                if (chr != nullptr && chr->prop >= 0) {
                    actions_.HitChr(*chr, command.S8(AiHitChrWithItemRecord::PART_NUM), Vec3{},
                                    command.U8(AiHitChrWithItemRecord::ITEM_NUM));
                }
                offset += AiHitChrWithItemRecord::kSize;
                break;
            }
            case AI_ChrHitChr: {
                Chr* shooter = chrOf(command.U8(AiChrHitChrRecord::CHR_NUM));
                Chr* target = chrOf(command.U8(AiChrHitChrRecord::CHR_NUM_TARGET));
                if (shooter != nullptr && target != nullptr && shooter->prop >= 0 && target->prop >= 0) {
                    int item = host_.HeldGunItem(*shooter, kGunRight);
                    if (item < 0) {
                        item = host_.HeldGunItem(*shooter, kGunLeft);
                    }
                    if (item >= 0) {
                        const Vec3 from = (*scene_.props)[shooter->prop].position;
                        const Vec3 to = (*scene_.props)[target->prop].position;
                        Vec3 direction = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
                        // guNormalize.
                        const float length = std::sqrt((direction[0] * direction[0]) + (direction[1] * direction[1]) +
                                                       (direction[2] * direction[2]));
                        if (length != 0.0f) {
                            const float scale = 1.0f / length;
                            direction = { direction[0] * scale, direction[1] * scale, direction[2] * scale };
                        }
                        actions_.HitChr(*target, command.S8(AiChrHitChrRecord::PART_NUM), direction, item);
                    }
                }
                offset += AiChrHitChrRecord::kSize;
                break;
            }
            case AI_TRYThrowingGrenade:
                jump(actions_.ThrowGrenade(self), AiTRYThrowingGrenadeRecord::kSize,
                     AiTRYThrowingGrenadeRecord::GOTOLABEL);
                break;
            case AI_TRYDroppingItem:
                jump(host_.DropNewItem(self, command.U16(AiTRYDroppingItemRecord::PROP_NUM),
                                       command.U8(AiTRYDroppingItemRecord::ITEM_NUM)),
                     AiTRYDroppingItemRecord::kSize, AiTRYDroppingItemRecord::GOTOLABEL);
                break;
            case AI_Surrender:
                if (IsNotDeadOrShot(self)) {
                    actions_.Surrender(self);
                }
                offset += AiSurrenderRecord::kSize;
                break;
            case AI_RemoveMe:
                actions_.FadeOut(self);
                offset += AiRemoveMeRecord::kSize;
                break;
            case AI_ChrRemoveInstant: {
                Chr* chr = chrOf(command.U8(AiChrRemoveInstantRecord::CHR_NUM));
                if (chr != nullptr && chr->prop >= 0) {
                    chr->hidden |= ChrHidden::Remove;
                }
                offset += AiChrRemoveInstantRecord::kSize;
                break;
            }
            case AI_TRYTriggeringAlarmAtPad:
                jump(StartAlarmAt(self, command.U16(AiTRYTriggeringAlarmAtPadRecord::PAD)),
                     AiTRYTriggeringAlarmAtPadRecord::kSize, AiTRYTriggeringAlarmAtPadRecord::GOTOLABEL);
                break;
            case AI_AlarmOn:
                host_.AlarmActivate();
                offset += AiAlarmOnRecord::kSize;
                break;
            case AI_AlarmOff:
                host_.AlarmDeactivate();
                offset += AiAlarmOffRecord::kSize;
                break;
            case AI_TRYRunFromBond:
                // removed_animation_routine_27.
                jump(false, AiTRYRunFromBondRecord::kSize, AiTRYRunFromBondRecord::GOTOLABEL);
                break;
            case AI_TRYRunToBond:
                jump(GoToBond(self, Speed::Run), AiTRYRunToBondRecord::kSize, AiTRYRunToBondRecord::GOTOLABEL);
                break;
            case AI_TRYWalkToBond:
                jump(GoToBond(self, Speed::Walk), AiTRYWalkToBondRecord::kSize, AiTRYWalkToBondRecord::GOTOLABEL);
                break;
            case AI_TRYSprintToBond:
                jump(GoToBond(self, Speed::Sprint), AiTRYSprintToBondRecord::kSize, AiTRYSprintToBondRecord::GOTOLABEL);
                break;
            case AI_TRYFindCover:
                // removed_animation_routine_2B.
                jump(false, AiTRYFindCoverRecord::kSize, AiTRYFindCoverRecord::GOTOLABEL);
                break;
            case AI_TRYRunToChr:
                jump(GoToChr(self, command.U8(AiTRYRunToChrRecord::CHR_NUM), Speed::Run), AiTRYRunToChrRecord::kSize,
                     AiTRYRunToChrRecord::GOTOLABEL);
                break;
            case AI_TRYWalkToChr:
                jump(GoToChr(self, command.U8(AiTRYWalkToChrRecord::CHR_NUM), Speed::Walk), AiTRYWalkToChrRecord::kSize,
                     AiTRYWalkToChrRecord::GOTOLABEL);
                break;
            case AI_TRYSprintToChr:
                jump(GoToChr(self, command.U8(AiTRYSprintToChrRecord::CHR_NUM), Speed::Sprint),
                     AiTRYSprintToChrRecord::kSize, AiTRYSprintToChrRecord::GOTOLABEL);
                break;
            case AI_SetNewRandom:
                self.random = static_cast<uint8_t>(RandomGetNext());
                offset += AiSetNewRandomRecord::kSize;
                break;
            case AI_IFRandomLessThan:
                jump(command.U8(AiIFRandomLessThanRecord::BYTE) > self.random, AiIFRandomLessThanRecord::kSize,
                     AiIFRandomLessThanRecord::GOTOLABEL);
                break;
            case AI_IFRandomGreaterThan:
                jump(command.U8(AiIFRandomGreaterThanRecord::BYTE) < self.random, AiIFRandomGreaterThanRecord::kSize,
                     AiIFRandomGreaterThanRecord::GOTOLABEL);
                break;
            case AI_RunToPad:
                GoToPad(self, command.U16(AiRunToPadRecord::PAD), Speed::Run);
                offset += AiRunToPadRecord::kSize;
                break;
            case AI_RunToPadPreset:
                GoToPad(self, self.padPreset1, Speed::Run);
                offset += AiRunToPadPresetRecord::kSize;
                break;
            case AI_WalkToPad:
                GoToPad(self, command.U16(AiWalkToPadRecord::PAD), Speed::Walk);
                offset += AiWalkToPadRecord::kSize;
                break;
            case AI_SprintToPad:
                GoToPad(self, command.U16(AiSprintToPadRecord::PAD), Speed::Sprint);
                offset += AiSprintToPadRecord::kSize;
                break;
            case AI_StartPatrol: {
                // if_actor_able_set_on_path.
                const int path = scene_.paths->FindPath(command.U8(AiStartPatrolRecord::PATH_NUM));
                if (path >= 0 && IsNotDeadOrShot(self)) {
                    actions_.StartPatrol(self, path);
                }
                offset += AiStartPatrolRecord::kSize;
                break;
            }
            case AI_IFICanHearAlarm:
                // chrCanHearAlarm is alarmIsActive.
                jump(host_.AlarmActive(), AiIFICanHearAlarmRecord::kSize, AiIFICanHearAlarmRecord::GOTOLABEL);
                break;
            case AI_IFAlarmIsOn:
                jump(host_.AlarmActive(), AiIFAlarmIsOnRecord::kSize, AiIFAlarmIsOnRecord::GOTOLABEL);
                break;
            case AI_IFGasIsLeaking:
                jump(host_.GasLeaking(), AiIFGasIsLeakingRecord::kSize, AiIFGasIsLeakingRecord::GOTOLABEL);
                break;
            case AI_IFIHeardBond:
                jump(IsHearingBond(self), AiIFIHeardBondRecord::kSize, AiIFIHeardBondRecord::GOTOLABEL);
                break;
            case AI_IFISeeSomeoneShot:
                jump(self.chrSeeShot >= 0, AiIFISeeSomeoneShotRecord::kSize, AiIFISeeSomeoneShotRecord::GOTOLABEL);
                break;
            case AI_IFISeeSomeoneDie:
                jump(self.chrSeeDie >= 0, AiIFISeeSomeoneDieRecord::kSize, AiIFISeeSomeoneDieRecord::GOTOLABEL);
                break;
            case AI_IFICouldSeeBond:
                jump(CanSeeBond(self), AiIFICouldSeeBondRecord::kSize, AiIFICouldSeeBondRecord::GOTOLABEL);
                break;
            case AI_IFICouldSeeBondsStan:
                jump(IsTargetNearlyInSight(self), AiIFICouldSeeBondsStanRecord::kSize,
                     AiIFICouldSeeBondsStanRecord::GOTOLABEL);
                break;
            case AI_IFIWasShotRecently:
                jump(SawTargetRecently(self), AiIFIWasShotRecentlyRecord::kSize, AiIFIWasShotRecentlyRecord::GOTOLABEL);
                break;
            case AI_IFIHeardBondRecently:
                jump(HeardTargetRecently(self), AiIFIHeardBondRecentlyRecord::kSize,
                     AiIFIHeardBondRecentlyRecord::GOTOLABEL);
                break;
            case AI_IFImInRoomWithChr: {
                Chr* chr = chrOf(command.U8(AiIFImInRoomWithChrRecord::CHR_NUM));
                bool pass = false;
                if (chr != nullptr && chr->prop >= 0) {
                    const Prop& theirs = (*scene_.props)[chr->prop];
                    pass = PositionInSameRoom(self, theirs.position, theirs.stan);
                }
                jump(pass, AiIFImInRoomWithChrRecord::kSize, AiIFImInRoomWithChrRecord::GOTOLABEL);
                break;
            }
            case AI_IFIveNotBeenSeen:
                jump((self.chrflags & ChrFlag::HasBeenOnScreen) == 0, AiIFIveNotBeenSeenRecord::kSize,
                     AiIFIveNotBeenSeenRecord::GOTOLABEL);
                break;
            case AI_IFImOnScreen:
                jump(self.prop >= 0 && ((*scene_.props)[self.prop].flags & PropFlag::OnScreen) != 0,
                     AiIFImOnScreenRecord::kSize, AiIFImOnScreenRecord::GOTOLABEL);
                break;
            case AI_IFMyRoomIsOnScreen:
                jump(self.prop >= 0 && host_.RoomRendered(scene_.stan->TileRoom((*scene_.props)[self.prop].stan)),
                     AiIFMyRoomIsOnScreenRecord::kSize, AiIFMyRoomIsOnScreenRecord::GOTOLABEL);
                break;
            case AI_IFRoomWithPadIsOnScreen: {
                // check_if_room_for_preset_loaded.
                const Pad* pad = PadForId(Chrs::ResolvePadId(self, command.U16(AiIFRoomWithPadIsOnScreenRecord::PAD)));
                jump(pad != nullptr && pad->stanTile != kNoTile &&
                         host_.RoomRendered(scene_.stan->TileRoom(pad->stanTile)),
                     AiIFRoomWithPadIsOnScreenRecord::kSize, AiIFRoomWithPadIsOnScreenRecord::GOTOLABEL);
                break;
            }
            case AI_IFImTargetedByBond: {
                // sub_GAME_7F0333F8: the player can see the character and aims
                // at its bounding sphere.
                bool pass = false;
                if (self.prop >= 0 && self.model) {
                    pass = PlayerCanSee(self);
                    if (pass) {
                        const float scale = self.boundingRadius * self.model->Playback().scale * 0.8f;
                        const Vec3& centre = (*scene_.props)[self.prop].position;
                        pass = host_.PlayerAimsAtSphere(centre, scale);
                    }
                }
                jump(pass, AiIFImTargetedByBondRecord::kSize, AiIFImTargetedByBondRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondMissedMe:
                jump((self.chrflags & ChrFlag::NearMiss) != 0, AiIFBondMissedMeRecord::kSize,
                     AiIFBondMissedMeRecord::GOTOLABEL);
                break;
            case AI_IFMyAngleToBondLessThan:
                jump(ByteToRadian(command.U8(AiIFMyAngleToBondLessThanRecord::ANGLE)) > AngleToBond(self),
                     AiIFMyAngleToBondLessThanRecord::kSize, AiIFMyAngleToBondLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMyAngleToBondGreaterThan:
                jump(ByteToRadian(command.U8(AiIFMyAngleToBondGreaterThanRecord::ANGLE)) < AngleToBond(self),
                     AiIFMyAngleToBondGreaterThanRecord::kSize, AiIFMyAngleToBondGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFMyAngleFromBondLessThan:
                jump(ByteToRadian(command.U8(AiIFMyAngleFromBondLessThanRecord::ANGLE)) > AngleFromBond(self),
                     AiIFMyAngleFromBondLessThanRecord::kSize, AiIFMyAngleFromBondLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMyAngleFromBondGreaterThan:
                jump(ByteToRadian(command.U8(AiIFMyAngleFromBondGreaterThanRecord::ANGLE)) < AngleFromBond(self),
                     AiIFMyAngleFromBondGreaterThanRecord::kSize, AiIFMyAngleFromBondGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFMyDistanceToBondLessThanDecimeter:
                jump(static_cast<float>(command.U16(AiIFMyDistanceToBondLessThanDecimeterRecord::DISTANCE)) * 10.0f >
                         DistanceToBond(self),
                     AiIFMyDistanceToBondLessThanDecimeterRecord::kSize,
                     AiIFMyDistanceToBondLessThanDecimeterRecord::GOTOLABEL);
                break;
            case AI_IFMyDistanceToBondGreaterThanDecimeter:
                jump(static_cast<float>(command.U16(AiIFMyDistanceToBondGreaterThanDecimeterRecord::DISTANCE)) * 10.0f <
                         DistanceToBond(self),
                     AiIFMyDistanceToBondGreaterThanDecimeterRecord::kSize,
                     AiIFMyDistanceToBondGreaterThanDecimeterRecord::GOTOLABEL);
                break;
            case AI_IFChrDistanceToPadLessThanDecimeter: {
                const Chr* chr = chrOf(command.U8(AiIFChrDistanceToPadLessThanDecimeterRecord::CHR_NUM));
                const float value =
                    static_cast<float>(command.U16(AiIFChrDistanceToPadLessThanDecimeterRecord::DISTANCE)) * 10.0f;
                jump(chr != nullptr &&
                         value > DistanceToPad(*chr, command.U16(AiIFChrDistanceToPadLessThanDecimeterRecord::PAD)),
                     AiIFChrDistanceToPadLessThanDecimeterRecord::kSize,
                     AiIFChrDistanceToPadLessThanDecimeterRecord::GOTOLABEL);
                break;
            }
            case AI_IFChrDistanceToPadGreaterThanDecimeter: {
                const Chr* chr = chrOf(command.U8(AiIFChrDistanceToPadGreaterThanDecimeterRecord::CHR_NUM));
                const float value =
                    static_cast<float>(command.U16(AiIFChrDistanceToPadGreaterThanDecimeterRecord::DISTANCE)) * 10.0f;
                jump(chr != nullptr &&
                         value < DistanceToPad(*chr, command.U16(AiIFChrDistanceToPadGreaterThanDecimeterRecord::PAD)),
                     AiIFChrDistanceToPadGreaterThanDecimeterRecord::kSize,
                     AiIFChrDistanceToPadGreaterThanDecimeterRecord::GOTOLABEL);
                break;
            }
            case AI_IFMyDistanceToChrLessThanDecimeter: {
                const float cutoff =
                    static_cast<float>(command.U16(AiIFMyDistanceToChrLessThanDecimeterRecord::DISTANCE)) * 10.0f;
                jump(cutoff > DistanceToChr(self, command.U8(AiIFMyDistanceToChrLessThanDecimeterRecord::CHR_NUM)),
                     AiIFMyDistanceToChrLessThanDecimeterRecord::kSize,
                     AiIFMyDistanceToChrLessThanDecimeterRecord::GOTOLABEL);
                break;
            }
            case AI_IFMyDistanceToChrGreaterThanDecimeter: {
                const float cutoff =
                    static_cast<float>(command.U16(AiIFMyDistanceToChrGreaterThanDecimeterRecord::DISTANCE)) * 10.0f;
                jump(cutoff < DistanceToChr(self, command.U8(AiIFMyDistanceToChrGreaterThanDecimeterRecord::CHR_NUM)),
                     AiIFMyDistanceToChrGreaterThanDecimeterRecord::kSize,
                     AiIFMyDistanceToChrGreaterThanDecimeterRecord::GOTOLABEL);
                break;
            }
            case AI_TRYSettingMyPresetToChrWithinDistanceDecimeter:
                jump(SetChrPresetNear(self, static_cast<float>(command.U16(
                                                AiTRYSettingMyPresetToChrWithinDistanceDecimeterRecord::DISTANCE)) *
                                                10.0f),
                     AiTRYSettingMyPresetToChrWithinDistanceDecimeterRecord::kSize,
                     AiTRYSettingMyPresetToChrWithinDistanceDecimeterRecord::GOTOLABEL);
                break;
            case AI_IFBondDistanceToPadLessThanDecimeter: {
                const float value =
                    static_cast<float>(command.U16(AiIFBondDistanceToPadLessThanDecimeterRecord::DISTANCE)) * 10.0f;
                jump(value >
                         DistanceFromBondToPad(self, command.U16(AiIFBondDistanceToPadLessThanDecimeterRecord::PAD)),
                     AiIFBondDistanceToPadLessThanDecimeterRecord::kSize,
                     AiIFBondDistanceToPadLessThanDecimeterRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondDistanceToPadGreaterThanDecimeter: {
                const float value =
                    static_cast<float>(command.U16(AiIFBondDistanceToPadGreaterThanDecimeterRecord::DISTANCE)) * 10.0f;
                jump(value <
                         DistanceFromBondToPad(self, command.U16(AiIFBondDistanceToPadGreaterThanDecimeterRecord::PAD)),
                     AiIFBondDistanceToPadGreaterThanDecimeterRecord::kSize,
                     AiIFBondDistanceToPadGreaterThanDecimeterRecord::GOTOLABEL);
                break;
            }
            case AI_IFChrInRoomWithPad: {
                // chrIfInPadRoom.
                const Chr* chr = chrOf(command.U8(AiIFChrInRoomWithPadRecord::CHR_NUM));
                const Pad* pad = PadForId(Chrs::ResolvePadId(self, command.U16(AiIFChrInRoomWithPadRecord::PAD)));
                bool pass = false;
                if (pad != nullptr && pad->stanTile != kNoTile && chr != nullptr && chr->prop >= 0) {
                    pass =
                        scene_.stan->TileRoom(pad->stanTile) == scene_.stan->TileRoom((*scene_.props)[chr->prop].stan);
                }
                jump(pass, AiIFChrInRoomWithPadRecord::kSize, AiIFChrInRoomWithPadRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondInRoomWithPad: {
                // check_if_actor_is_at_preset.
                const Pad* pad = PadForId(Chrs::ResolvePadId(self, command.U16(AiIFBondInRoomWithPadRecord::PAD)));
                bool pass = false;
                if (pad != nullptr && pad->stanTile != kNoTile && PlayerTile() != kNoTile) {
                    pass = scene_.stan->TileRoom(pad->stanTile) == scene_.stan->TileRoom(PlayerTile());
                }
                jump(pass, AiIFBondInRoomWithPadRecord::kSize, AiIFBondInRoomWithPadRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondCollectedObject: {
                const LinkTarget target = targetOf(command.U8(AiIFBondCollectedObjectRecord::OBJECT_TAG));
                jump(target.Exists() && target.Prop() >= 0 && host_.PlayerHasProp(target.Prop()),
                     AiIFBondCollectedObjectRecord::kSize, AiIFBondCollectedObjectRecord::GOTOLABEL);
                break;
            }
            case AI_IFKeyDropped:
                jump(host_.KeyDropped(command.U8(AiIFKeyDroppedRecord::KEY_ID)), AiIFKeyDroppedRecord::kSize,
                     AiIFKeyDroppedRecord::GOTOLABEL);
                break;
            case AI_IFItemIsAttachedToObject: {
                const LinkTarget target = targetOf(command.U8(AiIFItemIsAttachedToObjectRecord::OBJECT_TAG));
                bool pass = false;
                if (target.Exists() && target.Prop() >= 0) {
                    for (int child = (*scene_.props)[target.Prop()].child; child >= 0;
                         child = (*scene_.props)[child].prev) {
                        if ((*scene_.props)[child].type != PropType::Weapon) {
                            continue;
                        }
                        const PlacedObject* weapon = scene_.objects->ForProp(child);
                        if (weapon != nullptr &&
                            weapon->weaponNum == command.U8(AiIFItemIsAttachedToObjectRecord::ITEM_NUM)) {
                            pass = true;
                            break;
                        }
                    }
                }
                jump(pass, AiIFItemIsAttachedToObjectRecord::kSize, AiIFItemIsAttachedToObjectRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondHasItemEquipped: {
                const int item = command.U8(AiIFBondHasItemEquippedRecord::ITEM_NUM);
                jump(item == host_.PlayerWeapon(kGunRight) || item == host_.PlayerWeapon(kGunLeft),
                     AiIFBondHasItemEquippedRecord::kSize, AiIFBondHasItemEquippedRecord::GOTOLABEL);
                break;
            }
            case AI_IFObjectExists: {
                const LinkTarget target = targetOf(command.U8(AiIFObjectExistsRecord::OBJECT_TAG));
                jump(target.Exists() && target.Prop() >= 0, AiIFObjectExistsRecord::kSize,
                     AiIFObjectExistsRecord::GOTOLABEL);
                break;
            }
            case AI_IFObjectNotDestroyed: {
                const LinkTarget target = targetOf(command.U8(AiIFObjectNotDestroyedRecord::OBJECT_TAG));
                // objIsHealthy; a door has no destroyed state yet.
                jump(target.Exists() && target.Prop() >= 0 &&
                         (target.object == nullptr || Objects::IsHealthy(*target.object)),
                     AiIFObjectNotDestroyedRecord::kSize, AiIFObjectNotDestroyedRecord::GOTOLABEL);
                break;
            }
            case AI_IFObjectWasActivated: {
                const LinkTarget target = targetOf(command.U8(AiIFObjectWasActivatedRecord::OBJECT_TAG));
                bool pass = false;
                if (target.Exists() && target.Prop() >= 0 && (target.Runtime() & ObjectRuntime::Activated) != 0) {
                    target.Runtime() &= ~ObjectRuntime::Activated;
                    pass = true;
                }
                jump(pass, AiIFObjectWasActivatedRecord::kSize, AiIFObjectWasActivatedRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondUsedGadgetOnObject: {
                const LinkTarget target = targetOf(command.U8(AiIFBondUsedGadgetOnObjectRecord::OBJECT_TAG));
                bool pass = false;
                if (target.object != nullptr && target.Prop() >= 0 &&
                    (target.object->state & ObjectState::Activated) != 0) {
                    target.object->state &= static_cast<uint8_t>(~ObjectState::Activated);
                    pass = true;
                }
                jump(pass, AiIFBondUsedGadgetOnObjectRecord::kSize, AiIFBondUsedGadgetOnObjectRecord::GOTOLABEL);
                break;
            }
            case AI_ActivateObject: {
                const LinkTarget target = targetOf(command.U8(AiActivateObjectRecord::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    if (target.door != nullptr) {
                        scene_.doors->ActivateWrapper(static_cast<int>(target.door - scene_.doors->All().data()));
                    } else if (target.object != nullptr) {
                        host_.UseObject(target.Prop());
                    }
                }
                offset += AiActivateObjectRecord::kSize;
                break;
            }
            case AI_DestroyObject: {
                const LinkTarget target = targetOf(command.U8(AiDestroyObjectRecord::OBJECT_TAG));
                if (target.object != nullptr && target.Prop() >= 0 && Objects::IsHealthy(*target.object)) {
                    host_.DestroyObject(*target.object);
                }
                offset += AiDestroyObjectRecord::kSize;
                break;
            }
            case AI_DropObject: {
                const LinkTarget target = targetOf(command.U8(AiDropObjectRecord::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    const int parent = (*scene_.props)[target.Prop()].parent;
                    if (parent >= 0 && (*scene_.props)[parent].type == PropType::Chr) {
                        Chr& holder = (*scene_.chrs)[(*scene_.props)[parent].object];
                        host_.DropProp(target.Prop(), 2);
                        holder.hidden |= ChrHidden::DropHeldItems;
                    }
                }
                offset += AiDropObjectRecord::kSize;
                break;
            }
            case AI_ChrDropAllConcealedItems: {
                Chr* chr = chrOf(command.U8(AiChrDropAllConcealedItemsRecord::CHR_NUM));
                if (chr != nullptr && chr->prop >= 0) {
                    host_.DropConcealedItems(*chr);
                    chr->hidden |= ChrHidden::DropHeldItems;
                }
                offset += AiChrDropAllConcealedItemsRecord::kSize;
                break;
            }
            case AI_ChrDropAllHeldItems: {
                Chr* chr = chrOf(command.U8(AiChrDropAllHeldItemsRecord::CHR_NUM));
                if (chr != nullptr && chr->prop >= 0) {
                    if (chr->weaponsHeld[kGunRight] >= 0) {
                        host_.DropProp(chr->weaponsHeld[kGunRight], 1);
                        chr->hidden |= ChrHidden::DropHeldItems;
                    }
                    if (chr->weaponsHeld[kGunLeft] >= 0) {
                        host_.DropProp(chr->weaponsHeld[kGunLeft], 1);
                        chr->hidden |= ChrHidden::DropHeldItems;
                    }
                }
                offset += AiChrDropAllHeldItemsRecord::kSize;
                break;
            }
            case AI_BondCollectObject: {
                const LinkTarget target = targetOf(command.U8(AiBondCollectObjectRecord::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    host_.CollectProp(target.Prop());
                }
                offset += AiBondCollectObjectRecord::kSize;
                break;
            }
            case AI_ChrEquipObject: {
                const LinkTarget target = targetOf(command.U8(AiChrEquipObjectRecord::OBJECT_TAG));
                Chr* chr = chrOf(command.U8(AiChrEquipObjectRecord::CHR_NUM));
                if (target.Exists() && target.Prop() >= 0 && chr != nullptr) {
                    host_.EquipObject(target.Prop(), *chr);
                }
                offset += AiChrEquipObjectRecord::kSize;
                break;
            }
            case AI_MoveObject: {
                const LinkTarget target = targetOf(command.U8(AiMoveObjectRecord::OBJECT_TAG));
                if (target.object != nullptr && target.Prop() >= 0) {
                    host_.MoveObjectToPad(*target.object, command.U16(AiMoveObjectRecord::PAD));
                }
                offset += AiMoveObjectRecord::kSize;
                break;
            }
            case AI_DoorOpen: {
                const LinkTarget target = targetOf(command.U8(AiDoorOpenRecord::OBJECT_TAG));
                if (target.door != nullptr && target.Prop() >= 0) {
                    scene_.doors->Activate(static_cast<int>(target.door - scene_.doors->All().data()),
                                           DoorState::Opening);
                }
                offset += AiDoorOpenRecord::kSize;
                break;
            }
            case AI_DoorClose: {
                const LinkTarget target = targetOf(command.U8(AiDoorCloseRecord::OBJECT_TAG));
                if (target.door != nullptr && target.Prop() >= 0) {
                    scene_.doors->Activate(static_cast<int>(target.door - scene_.doors->All().data()),
                                           DoorState::Closing);
                }
                offset += AiDoorCloseRecord::kSize;
                break;
            }
            case AI_IFDoorStateEqual: {
                const LinkTarget target = targetOf(command.U8(AiIFDoorStateEqualRecord::OBJECT_TAG));
                bool pass = false;
                if (target.door != nullptr && target.Prop() >= 0) {
                    const PlacedDoor& door = *target.door;
                    const uint8_t wanted = command.U8(AiIFDoorStateEqualRecord::DOOR_STATE);
                    if (door.state == DoorState::Stationary) {
                        if (door.openPosition <= 0) {
                            pass = (wanted & kDoorStateClosed) != 0;
                        } else {
                            pass = (wanted & kDoorStateOpen) != 0;
                        }
                    } else if (door.state == DoorState::Opening || door.state == DoorState::Waiting) {
                        pass = (wanted & kDoorStateOpening) != 0;
                    } else if (door.state == DoorState::Closing) {
                        pass = (wanted & kDoorStateClosing) != 0;
                    }
                }
                jump(pass, AiIFDoorStateEqualRecord::kSize, AiIFDoorStateEqualRecord::GOTOLABEL);
                break;
            }
            case AI_IFDoorHasBeenOpenedBefore: {
                const LinkTarget target = targetOf(command.U8(AiIFDoorHasBeenOpenedBeforeRecord::OBJECT_TAG));
                jump(target.door != nullptr && target.Prop() >= 0 &&
                         (target.door->runtimeFlags & ObjectRuntime::BeenOpened) != 0,
                     AiIFDoorHasBeenOpenedBeforeRecord::kSize, AiIFDoorHasBeenOpenedBeforeRecord::GOTOLABEL);
                break;
            }
            case AI_DoorSetLock: {
                const LinkTarget target = targetOf(command.U8(AiDoorSetLockRecord::OBJECT_TAG));
                if (target.door != nullptr && target.Prop() >= 0) {
                    target.door->keyFlags |= command.U8(AiDoorSetLockRecord::LOCK_FLAG);
                }
                offset += AiDoorSetLockRecord::kSize;
                break;
            }
            case AI_DoorUnsetLock: {
                const LinkTarget target = targetOf(command.U8(AiDoorUnsetLockRecord::OBJECT_TAG));
                if (target.door != nullptr && target.Prop() >= 0) {
                    target.door->keyFlags &= ~static_cast<uint32_t>(command.U8(AiDoorUnsetLockRecord::LOCK_FLAG));
                }
                offset += AiDoorUnsetLockRecord::kSize;
                break;
            }
            case AI_IFDoorLockEqual: {
                const LinkTarget target = targetOf(command.U8(AiIFDoorLockEqualRecord::OBJECT_TAG));
                const uint32_t bits = command.U8(AiIFDoorLockEqualRecord::LOCK_FLAG);
                jump(target.door != nullptr && target.Prop() >= 0 && (target.door->keyFlags & bits) == bits,
                     AiIFDoorLockEqualRecord::kSize, AiIFDoorLockEqualRecord::GOTOLABEL);
                break;
            }
            case AI_IFObjectiveNumComplete: {
                const int objective = command.U8(AiIFObjectiveNumCompleteRecord::OBJ_NUM);
                jump(host_.ObjectiveCount() > objective && host_.ObjectiveComplete(objective),
                     AiIFObjectiveNumCompleteRecord::kSize, AiIFObjectiveNumCompleteRecord::GOTOLABEL);
                break;
            }
            case AI_TRYUnknown6e:
                jump(SetPadPresetByQuadrant(self, command.U8(AiTRYUnknown6eRecord::val)), AiTRYUnknown6eRecord::kSize,
                     AiTRYUnknown6eRecord::GOTOLABEL);
                break;
            case AI_TRYUnknown6f:
                jump(SetPresetToPlayerNeighbour(self, command.U8(AiTRYUnknown6fRecord::val)),
                     AiTRYUnknown6fRecord::kSize, AiTRYUnknown6fRecord::GOTOLABEL);
                break;
            case AI_IFMyNumArghsLessThan:
                jump(command.U8(AiIFMyNumArghsLessThanRecord::val) > self.numArghs, AiIFMyNumArghsLessThanRecord::kSize,
                     AiIFMyNumArghsLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMyNumArghsGreaterThan:
                jump(command.U8(AiIFMyNumArghsGreaterThanRecord::val) < self.numArghs,
                     AiIFMyNumArghsGreaterThanRecord::kSize, AiIFMyNumArghsGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFMyNumCloseArghsLessThan:
                jump(command.U8(AiIFMyNumCloseArghsLessThanRecord::val) > self.numCloseArghs,
                     AiIFMyNumCloseArghsLessThanRecord::kSize, AiIFMyNumCloseArghsLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMyNumCloseArghsGreaterThan:
                jump(command.U8(AiIFMyNumCloseArghsGreaterThanRecord::val) < self.numCloseArghs,
                     AiIFMyNumCloseArghsGreaterThanRecord::kSize, AiIFMyNumCloseArghsGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFChrHealthLessThan: {
                const float value = static_cast<float>(command.U8(AiIFChrHealthLessThanRecord::HEALTH)) * 0.1f;
                const Chr* chr = chrOf(command.U8(AiIFChrHealthLessThanRecord::CHR_NUM));
                jump(chr != nullptr && (chr->maxDamage - chr->damage) < value, AiIFChrHealthLessThanRecord::kSize,
                     AiIFChrHealthLessThanRecord::GOTOLABEL);
                break;
            }
            case AI_IFChrHealthGreaterThan: {
                const float value = static_cast<float>(command.U8(AiIFChrHealthGreaterThanRecord::HEALTH)) * 0.1f;
                const Chr* chr = chrOf(command.U8(AiIFChrHealthGreaterThanRecord::CHR_NUM));
                jump(chr != nullptr && (chr->maxDamage - chr->damage) > value, AiIFChrHealthGreaterThanRecord::kSize,
                     AiIFChrHealthGreaterThanRecord::GOTOLABEL);
                break;
            }
            case AI_IFChrWasDamagedSinceLastCheck: {
                Chr* chr = chrOf(command.U8(AiIFChrWasDamagedSinceLastCheckRecord::CHR_NUM));
                bool pass = false;
                if (chr != nullptr && (chr->chrflags & ChrFlag::WasDamaged) != 0) {
                    chr->chrflags &= ~ChrFlag::WasDamaged;
                    pass = true;
                }
                jump(pass, AiIFChrWasDamagedSinceLastCheckRecord::kSize,
                     AiIFChrWasDamagedSinceLastCheckRecord::GOTOLABEL);
                break;
            }
            case AI_IFBondHealthLessThan:
                jump(static_cast<float>(command.U8(AiIFBondHealthLessThanRecord::HEALTH)) / 255.0f >
                         host_.Player().health,
                     AiIFBondHealthLessThanRecord::kSize, AiIFBondHealthLessThanRecord::GOTOLABEL);
                break;
            case AI_IFBondHealthGreaterThan:
                jump(static_cast<float>(command.U8(AiIFBondHealthGreaterThanRecord::HEALTH)) / 255.0f <
                         host_.Player().health,
                     AiIFBondHealthGreaterThanRecord::kSize, AiIFBondHealthGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFGameDifficultyLessThan:
                jump(command.U8(AiIFGameDifficultyLessThanRecord::DIFICULTY_ID) > host_.Difficulty(),
                     AiIFGameDifficultyLessThanRecord::kSize, AiIFGameDifficultyLessThanRecord::GOTOLABEL);
                break;
            case AI_IFGameDifficultyGreaterThan:
                jump(command.U8(AiIFGameDifficultyGreaterThanRecord::DIFICULTY_ID) < host_.Difficulty(),
                     AiIFGameDifficultyGreaterThanRecord::kSize, AiIFGameDifficultyGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFMissionTimeLessThan:
                jump(static_cast<float>(command.U16(AiIFMissionTimeLessThanRecord::SECONDS)) > host_.StageSeconds(),
                     AiIFMissionTimeLessThanRecord::kSize, AiIFMissionTimeLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMissionTimeGreaterThan:
                jump(static_cast<float>(command.U16(AiIFMissionTimeGreaterThanRecord::SECONDS)) < host_.StageSeconds(),
                     AiIFMissionTimeGreaterThanRecord::kSize, AiIFMissionTimeGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFSystemPowerTimeLessThan:
                jump(static_cast<float>(command.U16(AiIFSystemPowerTimeLessThanRecord::MINUTES)) * kTickRate >
                         host_.PowerOnSeconds(),
                     AiIFSystemPowerTimeLessThanRecord::kSize, AiIFSystemPowerTimeLessThanRecord::GOTOLABEL);
                break;
            case AI_IFSystemPowerTimeGreaterThan:
                jump(static_cast<float>(command.U16(AiIFSystemPowerTimeGreaterThanRecord::MINUTES)) * kTickRate <
                         host_.PowerOnSeconds(),
                     AiIFSystemPowerTimeGreaterThanRecord::kSize, AiIFSystemPowerTimeGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFLevelIdLessThan:
                jump(command.U8(AiIFLevelIdLessThanRecord::LEVEL_ID) > host_.StageNumber(),
                     AiIFLevelIdLessThanRecord::kSize, AiIFLevelIdLessThanRecord::GOTOLABEL);
                break;
            case AI_IFLevelIdGreaterThan:
                jump(command.U8(AiIFLevelIdGreaterThanRecord::LEVEL_ID) < host_.StageNumber(),
                     AiIFLevelIdGreaterThanRecord::kSize, AiIFLevelIdGreaterThanRecord::GOTOLABEL);
                break;
            case AI_SetMyMorale:
                self.morale = command.U8(AiSetMyMoraleRecord::val);
                offset += AiSetMyMoraleRecord::kSize;
                break;
            case AI_AddToMyMorale:
                if (255 - command.U8(AiAddToMyMoraleRecord::val) < self.morale) {
                    self.morale = 255;
                } else {
                    self.morale = static_cast<uint8_t>(self.morale + command.U8(AiAddToMyMoraleRecord::val));
                }
                offset += AiAddToMyMoraleRecord::kSize;
                break;
            case AI_SubtractFromMyMorale:
                if (command.U8(AiSubtractFromMyMoraleRecord::val) > self.morale) {
                    self.morale = 0;
                } else {
                    self.morale = static_cast<uint8_t>(self.morale - command.U8(AiSubtractFromMyMoraleRecord::val));
                }
                offset += AiSubtractFromMyMoraleRecord::kSize;
                break;
            case AI_IFMyMoraleLessThan:
                jump(command.U8(AiIFMyMoraleLessThanRecord::val) > self.morale, AiIFMyMoraleLessThanRecord::kSize,
                     AiIFMyMoraleLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMyMoraleLessThanRandom:
                jump(self.morale < self.random, AiIFMyMoraleLessThanRandomRecord::kSize,
                     AiIFMyMoraleLessThanRandomRecord::GOTOLABEL);
                break;
            case AI_SetMyAlertness:
                self.alertness = command.U8(AiSetMyAlertnessRecord::val);
                offset += AiSetMyAlertnessRecord::kSize;
                break;
            case AI_AddToMyAlertness:
                if (255 - command.U8(AiAddToMyAlertnessRecord::val) < self.alertness) {
                    self.alertness = 255;
                } else {
                    self.alertness = static_cast<uint8_t>(self.alertness + command.U8(AiAddToMyAlertnessRecord::val));
                }
                offset += AiAddToMyAlertnessRecord::kSize;
                break;
            case AI_SubtractFromMyAlertness:
                if (command.U8(AiSubtractFromMyAlertnessRecord::val) > self.alertness) {
                    self.alertness = 0;
                } else {
                    self.alertness =
                        static_cast<uint8_t>(self.alertness - command.U8(AiSubtractFromMyAlertnessRecord::val));
                }
                offset += AiSubtractFromMyAlertnessRecord::kSize;
                break;
            case AI_IFMyAlertnessLessThan:
                jump(command.U8(AiIFMyAlertnessLessThanRecord::CHRBYTE) > self.alertness,
                     AiIFMyAlertnessLessThanRecord::kSize, AiIFMyAlertnessLessThanRecord::GOTOLABEL);
                break;
            case AI_IFMyAlertnessLessThanRandom:
                jump(self.alertness < self.random, AiIFMyAlertnessLessThanRandomRecord::kSize,
                     AiIFMyAlertnessLessThanRandomRecord::GOTOLABEL);
                break;
            case AI_SetMyHearingScale:
                self.hearingScale = static_cast<float>(command.U16(AiSetMyHearingScaleRecord::HEARING_SCALE)) / 1000.0f;
                offset += AiSetMyHearingScaleRecord::kSize;
                break;
            case AI_SetMyVisionRange:
                self.visionRange = static_cast<float>(command.U8(AiSetMyVisionRangeRecord::VISION_RANGE));
                offset += AiSetMyVisionRangeRecord::kSize;
                break;
            case AI_SetMyGrenadeProbability:
                self.grenadeProb = command.U8(AiSetMyGrenadeProbabilityRecord::GRENADE_PROB);
                offset += AiSetMyGrenadeProbabilityRecord::kSize;
                break;
            case AI_SetMyChrNum:
                self.chrnum = command.U8(AiSetMyChrNumRecord::CHR_NUM);
                offset += AiSetMyChrNumRecord::kSize;
                break;
            case AI_SetMyHealthTotal:
                // chrSetMaxDamage.
                self.maxDamage =
                    host_.Ai007HealthMod() * (static_cast<float>(command.U16(AiSetMyHealthTotalRecord::HEALTH)) * 0.1f);
                offset += AiSetMyHealthTotalRecord::kSize;
                break;
            case AI_SetMyArmour:
                // chrAddHealth.
                self.damage -=
                    host_.Ai007HealthMod() * (static_cast<float>(command.U16(AiSetMyArmourRecord::AMOUNT)) * 0.1f);
                offset += AiSetMyArmourRecord::kSize;
                break;
            case AI_SetMySpeedRating:
                self.speedRating = command.S8(AiSetMySpeedRatingRecord::val);
                offset += AiSetMySpeedRatingRecord::kSize;
                break;
            case AI_SetMyArghRating:
                self.arghRating = command.S8(AiSetMyArghRatingRecord::val);
                offset += AiSetMyArghRatingRecord::kSize;
                break;
            case AI_SetMyAccuracyRating:
                self.accuracyRating = command.S8(AiSetMyAccuracyRatingRecord::val);
                offset += AiSetMyAccuracyRatingRecord::kSize;
                break;
            case AI_SetMyFlags2:
                self.flags2 |= command.U8(AiSetMyFlags2Record::BITS);
                offset += AiSetMyFlags2Record::kSize;
                break;
            case AI_UnsetMyFlags2:
                self.flags2 &= static_cast<uint8_t>(~command.U8(AiUnsetMyFlags2Record::BITS));
                offset += AiUnsetMyFlags2Record::kSize;
                break;
            case AI_IFMyFlags2Has:
                jump((self.flags2 & command.U8(AiIFMyFlags2HasRecord::BITS)) != 0, AiIFMyFlags2HasRecord::kSize,
                     AiIFMyFlags2HasRecord::GOTOLABEL);
                break;
            case AI_SetChrBitfield: {
                if (Chr* chr = chrOf(command.U8(AiSetChrBitfieldRecord::CHR_NUM))) {
                    chr->flags2 |= command.U8(AiSetChrBitfieldRecord::BITS);
                }
                offset += AiSetChrBitfieldRecord::kSize;
                break;
            }
            case AI_UnsetChrBitfield: {
                if (Chr* chr = chrOf(command.U8(AiUnsetChrBitfieldRecord::CHR_NUM))) {
                    chr->flags2 &= static_cast<uint8_t>(~command.U8(AiUnsetChrBitfieldRecord::BITS));
                }
                offset += AiUnsetChrBitfieldRecord::kSize;
                break;
            }
            case AI_IFChrBitfieldHas: {
                const Chr* chr = chrOf(command.U8(AiIFChrBitfieldHasRecord::CHR_NUM));
                jump(chr != nullptr && (chr->flags2 & command.U8(AiIFChrBitfieldHasRecord::BITS)) != 0,
                     AiIFChrBitfieldHasRecord::kSize, AiIFChrBitfieldHasRecord::GOTOLABEL);
                break;
            }
            case AI_SetObjectiveBitfield:
                state_.objectiveRegisters |= command.U32(AiSetObjectiveBitfieldRecord::BITFIELD);
                offset += AiSetObjectiveBitfieldRecord::kSize;
                break;
            case AI_UnsetObjectiveBitfield:
                state_.objectiveRegisters &= ~command.U32(AiUnsetObjectiveBitfieldRecord::BITFIELD);
                offset += AiUnsetObjectiveBitfieldRecord::kSize;
                break;
            case AI_IFObjectiveBitfieldHas:
                jump((state_.objectiveRegisters & command.U32(AiIFObjectiveBitfieldHasRecord::BITS)) != 0,
                     AiIFObjectiveBitfieldHasRecord::kSize, AiIFObjectiveBitfieldHasRecord::GOTOLABEL);
                break;
            case AI_SetMychrflags:
                self.chrflags |= command.U32(AiSetMychrflagsRecord::CHRFLAGS);
                offset += AiSetMychrflagsRecord::kSize;
                break;
            case AI_UnsetMychrflags:
                self.chrflags &= ~command.U32(AiUnsetMychrflagsRecord::CHRFLAGS);
                offset += AiUnsetMychrflagsRecord::kSize;
                break;
            case AI_IFMychrflagsHas: {
                const uint32_t flags = command.U32(AiIFMychrflagsHasRecord::CHRFLAGS);
                jump((self.chrflags & flags) == flags, AiIFMychrflagsHasRecord::kSize,
                     AiIFMychrflagsHasRecord::GOTOLABEL);
                break;
            }
            case AI_SetChrchrflags: {
                if (Chr* chr = chrOf(command.U8(AiSetChrchrflagsRecord::CHR_NUM))) {
                    chr->chrflags |= command.U32(AiSetChrchrflagsRecord::CHRFLAGS);
                }
                offset += AiSetChrchrflagsRecord::kSize;
                break;
            }
            case AI_UnsetChrchrflags: {
                if (Chr* chr = chrOf(command.U8(AiUnsetChrchrflagsRecord::CHR_NUM))) {
                    chr->chrflags &= ~command.U32(AiUnsetChrchrflagsRecord::CHRFLAGS);
                }
                offset += AiUnsetChrchrflagsRecord::kSize;
                break;
            }
            case AI_IFChrchrflagsHas: {
                const uint32_t flags = command.U32(AiIFChrchrflagsHasRecord::CHRFLAGS);
                const Chr* chr = chrOf(command.U8(AiIFChrchrflagsHasRecord::CHR_NUM));
                jump(chr != nullptr && (chr->chrflags & flags) == flags, AiIFChrchrflagsHasRecord::kSize,
                     AiIFChrchrflagsHasRecord::GOTOLABEL);
                break;
            }
            case AI_SetObjectFlags: {
                const LinkTarget target = targetOf(command.U8(AiSetObjectFlagsRecord::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    ObjectFlags(target) |= command.U32(AiSetObjectFlagsRecord::BITFIELD);
                }
                offset += AiSetObjectFlagsRecord::kSize;
                break;
            }
            case AI_UnsetObjectFlags: {
                const LinkTarget target = targetOf(command.U8(AiUnsetObjectFlagsRecord::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    ObjectFlags(target) &= ~command.U32(AiUnsetObjectFlagsRecord::BITFIELD);
                }
                offset += AiUnsetObjectFlagsRecord::kSize;
                break;
            }
            case AI_IFObjectFlagsHas: {
                const LinkTarget target = targetOf(command.U8(AiIFObjectFlagsHasRecord::OBJECT_TAG));
                const uint32_t flags = command.U32(AiIFObjectFlagsHasRecord::BITS);
                jump(target.Exists() && target.Prop() >= 0 && (ObjectFlags(target) & flags) == flags,
                     AiIFObjectFlagsHasRecord::kSize, AiIFObjectFlagsHasRecord::GOTOLABEL);
                break;
            }
            case AI_SetObjectFlags2: {
                const LinkTarget target = targetOf(command.U8(AiSetObjectFlags2Record::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    target.Flags2() |= command.U32(AiSetObjectFlags2Record::BITS);
                }
                offset += AiSetObjectFlags2Record::kSize;
                break;
            }
            case AI_UnsetObjectFlags2: {
                const LinkTarget target = targetOf(command.U8(AiUnsetObjectFlags2Record::OBJECT_TAG));
                if (target.Exists() && target.Prop() >= 0) {
                    target.Flags2() &= ~command.U32(AiUnsetObjectFlags2Record::BITS);
                }
                offset += AiUnsetObjectFlags2Record::kSize;
                break;
            }
            case AI_IFObjectFlags2Has: {
                const LinkTarget target = targetOf(command.U8(AiIFObjectFlags2HasRecord::OBJECT_TAG));
                const uint32_t flags = command.U32(AiIFObjectFlags2HasRecord::BITFIELD);
                jump(target.Exists() && target.Prop() >= 0 && (target.Flags2() & flags) == flags,
                     AiIFObjectFlags2HasRecord::kSize, AiIFObjectFlags2HasRecord::GOTOLABEL);
                break;
            }
            case AI_SetMyChrPreset:
                self.chrPreset1 =
                    static_cast<int16_t>(scene_.chrs->ResolveId(self, command.U8(AiSetMyChrPresetRecord::PRESET)));
                offset += AiSetMyChrPresetRecord::kSize;
                break;
            case AI_SetChrChrPreset: {
                if (Chr* chr = chrOf(command.U8(AiSetChrChrPresetRecord::CHR_NUM))) {
                    chr->chrPreset1 =
                        static_cast<int16_t>(scene_.chrs->ResolveId(self, command.U8(AiSetChrChrPresetRecord::PRESET)));
                }
                offset += AiSetChrChrPresetRecord::kSize;
                break;
            }
            case AI_SetMyPadPreset: {
                const int pad = command.U16(AiSetMyPadPresetRecord::PAD_PRESET);
                if (subject.chr != nullptr) {
                    self.padPreset1 = static_cast<int16_t>(Chrs::ResolvePadId(self, pad));
                } else if (subject.aircraft) {
                    subject.object->ai.pad = pad;
                }
                offset += AiSetMyPadPresetRecord::kSize;
                break;
            }
            case AI_SetChrPadPreset: {
                if (Chr* chr = chrOf(command.U8(AiSetChrPadPresetRecord::CHR_NUM))) {
                    chr->padPreset1 = static_cast<int16_t>(
                        Chrs::ResolvePadId(self, command.U16(AiSetChrPadPresetRecord::PAD_PRESET)));
                }
                offset += AiSetChrPadPresetRecord::kSize;
                break;
            }
            case AI_PRINT:
                // The game prints the string to the debug console.
                offset += AiItemSize(bytes, size, offset);
                break;
            case AI_MyTimerStart:
                self.timer60 = 0;
                self.hidden |= ChrHidden::TimerActive;
                offset += AiMyTimerStartRecord::kSize;
                break;
            case AI_MyTimerReset:
                self.timer60 = 0;
                offset += AiMyTimerResetRecord::kSize;
                break;
            case AI_MyTimerPause:
                self.hidden &= ~ChrHidden::TimerActive;
                offset += AiMyTimerPauseRecord::kSize;
                break;
            case AI_MyTimerResume:
                self.hidden |= ChrHidden::TimerActive;
                offset += AiMyTimerResumeRecord::kSize;
                break;
            case AI_IFMyTimerIsNotRunning:
                jump((self.hidden & ChrHidden::TimerActive) == 0, AiIFMyTimerIsNotRunningRecord::kSize,
                     AiIFMyTimerIsNotRunningRecord::GOTOLABEL);
                break;
            case AI_IFMyTimerLessThanTicks: {
                const float value = static_cast<float>(command.U24(AiIFMyTimerLessThanTicksRecord::TICKS)) / kTickRate;
                jump(static_cast<float>(self.timer60) / kTickRate < value, AiIFMyTimerLessThanTicksRecord::kSize,
                     AiIFMyTimerLessThanTicksRecord::GOTOLABEL);
                break;
            }
            case AI_IFMyTimerGreaterThanTicks: {
                const float value =
                    static_cast<float>(command.U24(AiIFMyTimerGreaterThanTicksRecord::TICKS)) / kTickRate;
                jump(static_cast<float>(self.timer60) / kTickRate > value, AiIFMyTimerGreaterThanTicksRecord::kSize,
                     AiIFMyTimerGreaterThanTicksRecord::GOTOLABEL);
                break;
            }
            case AI_HudCountdownShow:
                host_.CountdownShow(true);
                offset += AiHudCountdownShowRecord::kSize;
                break;
            case AI_HudCountdownHide:
                host_.CountdownShow(false);
                offset += AiHudCountdownHideRecord::kSize;
                break;
            case AI_HudCountdownSet:
                host_.CountdownSet(static_cast<float>(command.U16(AiHudCountdownSetRecord::SECONDS)) * kTickRate);
                offset += AiHudCountdownSetRecord::kSize;
                break;
            case AI_HudCountdownStop:
                host_.CountdownRunning(false);
                offset += AiHudCountdownStopRecord::kSize;
                break;
            case AI_HudCountdownStart:
                host_.CountdownRunning(true);
                offset += AiHudCountdownStartRecord::kSize;
                break;
            case AI_IFHudCountdownIsNotRunning:
                jump(!host_.CountdownIsRunning(), AiIFHudCountdownIsNotRunningRecord::kSize,
                     AiIFHudCountdownIsNotRunningRecord::GOTOLABEL);
                break;
            case AI_IFHudCountdownLessThan:
                jump(host_.CountdownValue() <
                         static_cast<float>(command.U16(AiIFHudCountdownLessThanRecord::SECONDS)) * kTickRate,
                     AiIFHudCountdownLessThanRecord::kSize, AiIFHudCountdownLessThanRecord::GOTOLABEL);
                break;
            case AI_IFHudCountdownGreaterThan:
                jump(host_.CountdownValue() >
                         static_cast<float>(command.U16(AiIFHudCountdownGreaterThanRecord::SECONDS)) * kTickRate,
                     AiIFHudCountdownGreaterThanRecord::kSize, AiIFHudCountdownGreaterThanRecord::GOTOLABEL);
                break;
            case AI_TRYSpawningChrAtPad: {
                const AiList* spawnList = scene_.lists->FindById(command.U16(AiTRYSpawningChrAtPadRecord::AI_LIST_ID));
                Chr* spawned = nullptr;
                jump(SpawnAtPad(self, command.U8(AiTRYSpawningChrAtPadRecord::BODY_NUM),
                                command.S8(AiTRYSpawningChrAtPadRecord::HEAD_NUM),
                                command.U16(AiTRYSpawningChrAtPadRecord::PAD), spawnList,
                                command.U32(AiTRYSpawningChrAtPadRecord::BITFIELD), &spawned),
                     AiTRYSpawningChrAtPadRecord::kSize, AiTRYSpawningChrAtPadRecord::GOTOLABEL);
                break;
            }
            case AI_TRYSpawningChrNextToChr: {
                const AiList* spawnList =
                    scene_.lists->FindById(command.U16(AiTRYSpawningChrNextToChrRecord::AI_LIST_ID));
                Chr* spawned = nullptr;
                jump(SpawnAtChr(self, command.U8(AiTRYSpawningChrNextToChrRecord::BODY_NUM),
                                command.S8(AiTRYSpawningChrNextToChrRecord::HEAD_NUM),
                                command.U8(AiTRYSpawningChrNextToChrRecord::CHR_NUM_TARGET), spawnList,
                                command.U32(AiTRYSpawningChrNextToChrRecord::BITFIELD), &spawned),
                     AiTRYSpawningChrNextToChrRecord::kSize, AiTRYSpawningChrNextToChrRecord::GOTOLABEL);
                break;
            }
            case AI_TRYGiveMeItem: {
                bool given = false;
                if (self.prop >= 0 && self.model) {
                    const int item = command.U8(AiTRYGiveMeItemRecord::ITEM_NUM);
                    const uint32_t flags = command.U32(AiTRYGiveMeItemRecord::PROPFLAG);
                    if (host_.CheatActive(Game::Menu::Cheat::kEnemyRockets) && EnemyRocketsReplaces(item)) {
                        // The case has no break: the character's own weapon is
                        // made as well, and its result is the command's.
                        host_.GiveWeapon(self, kPropChrRocketLaunch, kItemRocketLauncher, flags);
                    }
                    given = host_.GiveWeapon(self, command.U16(AiTRYGiveMeItemRecord::PROP_NUM), item, flags);
                }
                jump(given, AiTRYGiveMeItemRecord::kSize, AiTRYGiveMeItemRecord::GOTOLABEL);
                break;
            }
            case AI_TRYGiveMeHat: {
                bool given = false;
                if (self.prop >= 0 && self.model) {
                    given = host_.GiveHat(self, command.U16(AiTRYGiveMeHatRecord::PROP_NUM),
                                          command.U32(AiTRYGiveMeHatRecord::PROP_BITFIELD));
                }
                jump(given, AiTRYGiveMeHatRecord::kSize, AiTRYGiveMeHatRecord::GOTOLABEL);
                break;
            }
            case AI_TRYCloningChr:
                jump(Clone(self, command), AiTRYCloningChrRecord::kSize, AiTRYCloningChrRecord::GOTOLABEL);
                break;
            case AI_TextPrintBottom:
                host_.ShowBottomText(command.U16(AiTextPrintBottomRecord::txt));
                offset += AiTextPrintBottomRecord::kSize;
                break;
            case AI_TextPrintTop:
                host_.ShowTopText(command.U16(AiTextPrintTopRecord::txt));
                offset += AiTextPrintTopRecord::kSize;
                break;
            case AI_SfxPlay:
                host_.SfxPlay(command.S8(AiSfxPlayRecord::CHANNEL_NUM), command.S16(AiSfxPlayRecord::SOUND_NUM));
                offset += AiSfxPlayRecord::kSize;
                break;
            case AI_SfxStopChannel:
                host_.SfxStop(command.S8(AiSfxStopChannelRecord::CHANNEL_NUM));
                offset += AiSfxStopChannelRecord::kSize;
                break;
            case AI_SfxSetChannelVolume: {
                const int slot = command.S8(AiSfxSetChannelVolumeRecord::slotID);
                if (slot >= 0 && slot < 8) {
                    host_.SfxSetVolume(slot, command.S16(AiSfxSetChannelVolumeRecord::TARGET_VOLUME),
                                       command.U16(AiSfxSetChannelVolumeRecord::sfxID));
                }
                offset += AiSfxSetChannelVolumeRecord::kSize;
                break;
            }
            case AI_SfxFadeChannelVolume: {
                const int slot = command.S8(AiSfxFadeChannelVolumeRecord::slotID);
                if (slot >= 0 && slot < 8) {
                    host_.SfxFadeVolume(slot,
                                        static_cast<float>(command.U16(AiSfxFadeChannelVolumeRecord::TARGET_VOLUME)),
                                        command.U16(AiSfxFadeChannelVolumeRecord::sfxID));
                }
                offset += AiSfxFadeChannelVolumeRecord::kSize;
                break;
            }
            case AI_SfxEmitFromObject: {
                const LinkTarget target = targetOf(command.U8(AiSfxEmitFromObjectRecord::OBJECT_TAG));
                const int slot = command.S8(AiSfxEmitFromObjectRecord::slotID);
                if (slot >= 0 && slot < 8 && target.Exists()) {
                    host_.SfxEmitFromProp(slot, target.Prop(), command.U16(AiSfxEmitFromObjectRecord::sfxID));
                }
                offset += AiSfxEmitFromObjectRecord::kSize;
                break;
            }
            case AI_SfxEmitFromPad: {
                const int slot = command.S8(AiSfxEmitFromPadRecord::slotID);
                const int pad = command.U16(AiSfxEmitFromPadRecord::PAD);
                if (slot >= 0 && slot < 8 && PadForId(pad) != nullptr) {
                    host_.SfxEmitFromPad(slot, pad, command.U16(AiSfxEmitFromPadRecord::sfxID));
                }
                offset += AiSfxEmitFromPadRecord::kSize;
                break;
            }
            case AI_IFSfxChannelVolumeLessThan: {
                const int slot = command.S8(AiIFSfxChannelVolumeLessThanRecord::slotID);
                jump(slot >= 0 && slot < 8 &&
                         host_.SfxChannelVolume(slot) < command.S16(AiIFSfxChannelVolumeLessThanRecord::VOLUME),
                     AiIFSfxChannelVolumeLessThanRecord::kSize, AiIFSfxChannelVolumeLessThanRecord::GOTOLABEL);
                break;
            }
            case AI_VehicleStartPath:
                if (subject.object != nullptr && !subject.aircraft) {
                    subject.object->ai.path = scene_.paths->FindPath(command.U8(AiVehicleStartPathRecord::PATH_NUM));
                    subject.object->ai.nextStep = 0;
                }
                offset += AiVehicleStartPathRecord::kSize;
                break;
            case AI_VehicleSpeed:
                if (subject.object != nullptr && !subject.aircraft) {
                    subject.object->ai.speedAim =
                        static_cast<float>(command.U16(AiVehicleSpeedRecord::TOP_SPEED)) * 100.0f / 15360.0f;
                    subject.object->ai.speedTime60 =
                        static_cast<float>(command.U16(AiVehicleSpeedRecord::ACCELERATION_TIME60));
                }
                offset += AiVehicleSpeedRecord::kSize;
                break;
            case AI_AircraftRotorSpeed:
                if (subject.object != nullptr && subject.aircraft) {
                    subject.object->ai.rotorSpeedAim =
                        static_cast<float>(command.U16(AiAircraftRotorSpeedRecord::ROTOR_SPEED)) * kTau / 3600.0f;
                    subject.object->ai.rotorSpeedTime =
                        static_cast<float>(command.U16(AiAircraftRotorSpeedRecord::ACCELERATION_TIME60));
                }
                offset += AiAircraftRotorSpeedRecord::kSize;
                break;
            case AI_IFCameraIsInIntro:
                jump(host_.CameraMode() == kCameraModeIntro1 || host_.CameraMode() == kCameraModeIntro2,
                     AiIFCameraIsInIntroRecord::kSize, AiIFCameraIsInIntroRecord::GOTOLABEL);
                break;
            case AI_IFCameraIsInBondSwirl:
                jump(host_.CameraMode() == kCameraModeSwirl, AiIFCameraIsInBondSwirlRecord::kSize,
                     AiIFCameraIsInBondSwirlRecord::GOTOLABEL);
                break;
            case AI_TvChangeScreenBank: {
                const LinkTarget target = targetOf(command.U8(AiTvChangeScreenBankRecord::OBJECT_TAG));
                if (target.object != nullptr && target.Prop() >= 0) {
                    host_.SetMonitorImage(*target.object, command.U8(AiTvChangeScreenBankRecord::SCREEN_INDEX),
                                          command.U8(AiTvChangeScreenBankRecord::SCREEN_BANK));
                }
                offset += AiTvChangeScreenBankRecord::kSize;
                break;
            }
            case AI_IFBondInTank:
                jump(host_.Player().inTank, AiIFBondInTankRecord::kSize, AiIFBondInTankRecord::GOTOLABEL);
                break;
            case AI_EndLevel:
                host_.EndLevel();
                offset += AiEndLevelRecord::kSize;
                break;
            case AI_CameraReturnToBond:
                host_.CameraReturnToBond();
                offset += AiCameraReturnToBondRecord::kSize;
                break;
            case AI_CameraLookAtBondFromPad:
                host_.CameraLookAtBondFromPad(command.U16(AiCameraLookAtBondFromPadRecord::PAD));
                offset += AiCameraLookAtBondFromPadRecord::kSize;
                break;
            case AI_CameraSwitch: {
                const PropDef* camera = scene_.links->FindTaggedRecord(command.U8(AiCameraSwitchRecord::OBJECT_TAG));
                if (camera != nullptr) {
                    host_.CameraSwitch(*camera, command.U16(AiCameraSwitchRecord::LOOK_AT_BOND_FLAG),
                                       command.U16(AiCameraSwitchRecord::UNUSED_FLAG));
                }
                offset += AiCameraSwitchRecord::kSize;
                break;
            }
            case AI_IFBondYPosLessThan: {
                const float position = static_cast<float>(command.S16(AiIFBondYPosLessThanRecord::Y_POS));
                jump(PlayerPosition()[1] < position, AiIFBondYPosLessThanRecord::kSize,
                     AiIFBondYPosLessThanRecord::GOTOLABEL);
                break;
            }
            case AI_BondDisableControl:
                host_.BondControl(false, command.U8(AiBondDisableControlRecord::val));
                offset += AiBondDisableControlRecord::kSize;
                break;
            case AI_BondEnableControl:
                host_.BondControl(true, 0);
                offset += AiBondEnableControlRecord::kSize;
                break;
            case AI_TRYTeleportingChrToPad: {
                Chr* chr = chrOf(command.U8(AiTRYTeleportingChrToPadRecord::CHR_NUM));
                jump(chr != nullptr && TeleportChrToPad(self, *chr, command.U16(AiTRYTeleportingChrToPadRecord::PAD)),
                     AiTRYTeleportingChrToPadRecord::kSize, AiTRYTeleportingChrToPadRecord::GOTOLABEL);
                break;
            }
            case AI_ScreenFadeToBlack:
                if (host_.StopTime() != 2) {
                    host_.ScreenFade(true);
                }
                offset += AiScreenFadeToBlackRecord::kSize;
                break;
            case AI_ScreenFadeFromBlack:
                if (host_.StopTime() != 2) {
                    host_.ScreenFade(false);
                }
                offset += AiScreenFadeFromBlackRecord::kSize;
                break;
            case AI_IFScreenFadeCompleted:
                jump(host_.ScreenFadeCompleted(), AiIFScreenFadeCompletedRecord::kSize,
                     AiIFScreenFadeCompletedRecord::GOTOLABEL);
                break;
            case AI_HideAllChrs:
                for (Chr& chr : scene_.chrs->Slots()) {
                    if (chr.inUse) {
                        chr.chrflags |= ChrFlag::Hidden;
                    }
                }
                offset += AiHideAllChrsRecord::kSize;
                break;
            case AI_ShowAllChrs:
                for (Chr& chr : scene_.chrs->Slots()) {
                    chr.chrflags &= ~ChrFlag::Hidden;
                }
                offset += AiShowAllChrsRecord::kSize;
                break;
            case AI_DoorOpenInstant: {
                const LinkTarget target = targetOf(command.U8(AiDoorOpenInstantRecord::OBJECT_TAG));
                if (target.door != nullptr && target.Prop() >= 0) {
                    scene_.doors->OpenInstant(static_cast<int>(target.door - scene_.doors->All().data()),
                                              timing.globalTimer);
                }
                offset += AiDoorOpenInstantRecord::kSize;
                break;
            }
            case AI_ChrRemoveItemInHand: {
                if (Chr* chr = chrOf(command.U8(AiChrRemoveItemInHandRecord::CHR_NUM))) {
                    host_.RemoveHeldItem(*chr, command.U8(AiChrRemoveItemInHandRecord::HAND_INDEX));
                }
                offset += AiChrRemoveItemInHandRecord::kSize;
                break;
            }
            case AI_IfNumberOfActivePlayersLessThan:
                jump(host_.PlayerCount() < command.S8(AiIfNumberOfActivePlayersLessThanRecord::NUMBER),
                     AiIfNumberOfActivePlayersLessThanRecord::kSize,
                     AiIfNumberOfActivePlayersLessThanRecord::GOTOLABEL);
                break;
            case AI_IFBondItemTotalAmmoLessThan:
                jump(host_.PlayerAmmo(command.S8(AiIFBondItemTotalAmmoLessThanRecord::ITEM_NUM)) <
                         command.S8(AiIFBondItemTotalAmmoLessThanRecord::AMMO_TOTAL),
                     AiIFBondItemTotalAmmoLessThanRecord::kSize, AiIFBondItemTotalAmmoLessThanRecord::GOTOLABEL);
                break;
            case AI_BondEquipItem:
                host_.PlayerEquip(kGunRight, command.S8(AiBondEquipItemRecord::ITEM_NUM));
                host_.PlayerEquip(kGunLeft, 0);
                offset += AiBondEquipItemRecord::kSize;
                break;
            case AI_BondEquipItemCinema:
                host_.PlayerUnequip(kGunRight, command.S8(AiBondEquipItemCinemaRecord::ITEM_NUM));
                host_.PlayerUnequip(kGunLeft, 0);
                offset += AiBondEquipItemCinemaRecord::kSize;
                break;
            case AI_BondSetLockedVelocity:
                host_.BondForceMove(static_cast<float>(command.S8(AiBondSetLockedVelocityRecord::X_SPEED60)),
                                    static_cast<float>(command.S8(AiBondSetLockedVelocityRecord::Z_SPEED60)));
                offset += AiBondSetLockedVelocityRecord::kSize;
                break;
            case AI_IFObjectInRoomWithPad: {
                const Pad* pad = PadForId(command.U16(AiIFObjectInRoomWithPadRecord::PAD));
                const LinkTarget target = targetOf(command.U8(AiIFObjectInRoomWithPadRecord::OBJECT_TAG));
                bool pass = false;
                if (pad != nullptr && pad->stanTile != kNoTile && target.Exists() && target.Prop() >= 0) {
                    const int stan = (*scene_.props)[target.Prop()].stan;
                    pass = stan != kNoTile && scene_.stan->TileRoom(pad->stanTile) == scene_.stan->TileRoom(stan);
                }
                jump(pass, AiIFObjectInRoomWithPadRecord::kSize, AiIFObjectInRoomWithPadRecord::GOTOLABEL);
                break;
            }
            case AI_SwitchSky:
                host_.SwitchSky();
                offset += AiSwitchSkyRecord::kSize;
                break;
            case AI_TriggerFadeAndExitLevelOnButtonPress:
                if (host_.StopTime() == 0) {
                    host_.SetStopTime(1);
                }
                offset += AiTriggerFadeAndExitLevelOnButtonPressRecord::kSize;
                break;
            case AI_IFBondIsDead:
                jump(host_.Player().dead, AiIFBondIsDeadRecord::kSize, AiIFBondIsDeadRecord::GOTOLABEL);
                break;
            case AI_BondDisableDamageAndPickups:
                host_.SetPlayerInvincible(true);
                offset += AiBondDisableDamageAndPickupsRecord::kSize;
                break;
            case AI_BondHideWeapons:
                host_.PlayerRemoveItemInHand(kGunRight);
                host_.PlayerRemoveItemInHand(kGunLeft);
                offset += AiBondHideWeaponsRecord::kSize;
                break;
            case AI_CameraOrbitPad:
                host_.CameraOrbitPad(command.U16(AiCameraOrbitPadRecord::PAD),
                                     command.U16(AiCameraOrbitPadRecord::LAT_DISTANCE),
                                     command.S16(AiCameraOrbitPadRecord::VERT_DISTANCE),
                                     command.S16(AiCameraOrbitPadRecord::ORBIT_SPEED60),
                                     command.S16(AiCameraOrbitPadRecord::Y_POS_OFFSET),
                                     command.U16(AiCameraOrbitPadRecord::INITIAL_ROTATION));
                offset += AiCameraOrbitPadRecord::kSize;
                break;
            case AI_CreditsRoll:
                host_.SetCredits(1);
                offset += AiCreditsRollRecord::kSize;
                break;
            case AI_IFCreditsHasCompleted:
                jump(host_.Credits() == 2, AiIFCreditsHasCompletedRecord::kSize,
                     AiIFCreditsHasCompletedRecord::GOTOLABEL);
                break;
            case AI_IFObjectiveAllCompleted:
                jump(host_.AllObjectivesComplete(), AiIFObjectiveAllCompletedRecord::kSize,
                     AiIFObjectiveAllCompletedRecord::GOTOLABEL);
                break;
            case AI_IFFolderActorIsEqual:
                jump(host_.FolderActor() == command.S8(AiIFFolderActorIsEqualRecord::BOND_ACTOR_INDEX),
                     AiIFFolderActorIsEqualRecord::kSize, AiIFFolderActorIsEqualRecord::GOTOLABEL);
                break;
            case AI_IFBondDamageAndPickupsDisabled:
                jump(host_.PlayerInvincible(), AiIFBondDamageAndPickupsDisabledRecord::kSize,
                     AiIFBondDamageAndPickupsDisabledRecord::GOTOLABEL);
                break;
            case AI_MusicPlaySlot:
                offset += AiMusicPlaySlotRecord::kSize;
                host_.MusicPlaySlot(command.S8(AiMusicPlaySlotRecord::MUSIC_SLOT),
                                    command.U8(AiMusicPlaySlotRecord::SECONDS_STOPPED_DURATION),
                                    command.U8(AiMusicPlaySlotRecord::SECONDS_TOTAL_DURATION));
                break;
            case AI_MusicStopSlot:
                offset += AiMusicStopSlotRecord::kSize;
                host_.MusicStopSlot(command.S8(AiMusicStopSlotRecord::MUSIC_SLOT));
                break;
            case AI_TriggerExplosionsAroundBond:
                host_.ExplodeAroundBond();
                offset += AiTriggerExplosionsAroundBondRecord::kSize;
                break;
            case AI_IFKilledCiviliansGreaterThan:
                jump(command.U8(AiIFKilledCiviliansGreaterThanRecord::CIVILIANS_KILLED) < host_.CiviliansKilled(),
                     AiIFKilledCiviliansGreaterThanRecord::kSize, AiIFKilledCiviliansGreaterThanRecord::GOTOLABEL);
                break;
            case AI_IFChrWasShotSinceLastCheck: {
                Chr* chr = chrOf(command.U8(AiIFChrWasShotSinceLastCheckRecord::CHR_NUM));
                bool pass = false;
                if (chr != nullptr && (chr->chrflags & ChrFlag::WasHit) != 0) {
                    chr->chrflags &= ~ChrFlag::WasHit;
                    pass = true;
                }
                jump(pass, AiIFChrWasShotSinceLastCheckRecord::kSize, AiIFChrWasShotSinceLastCheckRecord::GOTOLABEL);
                break;
            }
            case AI_BondKilledInAction:
                host_.SetBondKilledInAction();
                offset += AiBondKilledInActionRecord::kSize;
                break;
            case AI_RaiseArms:
                if (IsNotDeadOrShot(self)) {
                    actions_.RaiseArms(self);
                }
                offset += AiRaiseArmsRecord::kSize;
                break;
            case AI_GasLeakAndFadeFog:
                host_.GasLeak();
                offset += AiGasLeakAndFadeFogRecord::kSize;
                break;
            case AI_ObjectRocketLaunch: {
                const LinkTarget target = targetOf(command.U8(AiObjectRocketLaunchRecord::OBJECT_TAG));
                if (target.object != nullptr && target.Prop() >= 0) {
                    host_.LaunchObject(*target.object);
                }
                offset += AiObjectRocketLaunchRecord::kSize;
                break;
            }
            default:
                // A byte that is no command: the game steps over it.
                offset += AiItemSize(bytes, size, offset);
                break;
        }
    }
}

} // namespace Frigate::Level
