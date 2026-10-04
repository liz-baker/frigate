#include "level/Projectiles.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

#include "level/ChrActions.h"
#include "level/HitParts.h"
#include "level/ModelHit.h"
#include "level/WeaponTable.h"
#include "model/Quaternion.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kFraction = 2.3283064e-10f;
constexpr float kEpsilon = FLT_EPSILON * 16;
// What a moving object collides with on the stan.
constexpr int kMoveCdTypes = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;

bool IsCollectable(const PlacedObject& object) {
    return object.type == static_cast<uint8_t>(PropDefType::Collectable);
}

// objGetWidth. The decomp compares the object's PROPDEF type with the number
// PROP_TYPE_WEAPON, so it is keys that are wider.
float ObjectWidth(const PlacedObject& object) {
    return object.type == static_cast<uint8_t>(PropDefType::Key) ? 20.0f : 10.0f;
}

float Length(const Vec3& v) {
    return std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

// matrix_4x4_get_rotation_around_xyz.
Vec3 RotationAngles(const Mtxf& matrix) {
    const float norm = std::sqrt((matrix.m[1][2] * matrix.m[1][2]) + (matrix.m[2][2] * matrix.m[2][2]));
    if (kEpsilon < norm) {
        return { GameAtan2f(matrix.m[1][2], matrix.m[2][2]), GameAtan2f(-matrix.m[0][2], norm),
                 GameAtan2f(matrix.m[0][1], matrix.m[0][0]) };
    }
    return { 0.0f, GameAtan2f(-matrix.m[0][2], norm), GameAtan2f(-matrix.m[1][0], matrix.m[1][1]) };
}

// matrix_4x4_set_rotation_inverse.
Mtxf InverseRotation(const Mtxf& rotation) {
    Mtxf out = IdentityMtxf();
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            out.m[row][column] = rotation.m[column][row];
        }
    }
    return out;
}

Quaternion QuaternionOf(const Vec3& angles) {
    return EulerToQuaternion({ angles[0], angles[1], angles[2] });
}

std::array<float, 4> ToArray(const Quaternion& q) {
    return { q.w, q.x, q.y, q.z };
}

Quaternion FromArray(const std::array<float, 4>& a) {
    return { a[0], a[1], a[2], a[3] };
}

// sub_GAME_7F043838: the matrix that stands an object's up axis on a surface
// with the given normal.
Mtxf SurfaceMatrix(const Vec3& normal) {
    const float length = Length(normal);
    const float x = normal[0] / length;
    const float y = normal[1] / length;
    const float z = normal[2] / length;

    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 0.0f;
    float e = 0.0f;
    if (x == 0.0f && z == 0.0f) {
        a = 0.0f;
        b = 0.0f;
        c = y;
        d = 1.0f;
        e = 0.0f;
    } else {
        const float horizontal = std::sqrt((x * x) + (z * z));
        const float unitX = x / horizontal;
        d = z / horizontal;
        e = -unitX;
        a = y * unitX;
        b = -horizontal;
        c = y * d;
    }

    const float heading = GameAtan2f(d, e);
    const Mtxf yaw = RotationY(-heading);
    const Vec3 rotated = RotateVector(yaw, { a, b, c });
    const float pitch = GameAtan2f(rotated[0], rotated[1]);
    const Mtxf first = RotationY(-1.5707964f + heading);
    const Mtxf second = RotationX(-1.5707964f - pitch);
    return Multiply(first, second);
}

} // namespace

void Projectiles::Reset() {
    remoteMineTrigger_ = 0;
    proximityMines_.fill(-1);
    contact_ = ProjectileContact{};
}

float Projectiles::RandomFraction() {
    return static_cast<float>(shots_->Scene().random()) * kFraction;
}

Mtxf Projectiles::RandomRotation() {
    // mtxLoadRandomRotation. The US game's constants are scaled by a full turn;
    // the 7.5398-scaled ones the decomp's non-debug branch lists are not in the
    // US data.
    Vec3 angles{};
    for (float& angle : angles) {
        angle = RandomFraction() * kTau * 0.03125f - 0.09817477f;
    }
    return RotationXYZ(angles);
}

// -- Rooms along a path ---------------------------------------------------------------------

void Projectiles::FindRooms(const Vec3& from, const Vec3& to, const std::array<uint8_t, 8>& fromRooms,
                            std::array<uint8_t, 8>* finalRooms, std::vector<int>* traversed, int maxTraversed) const {
    // bgFindRoomsAlongSegment.
    const BgLevel& level = *shots_->Scene().level;
    const std::vector<BgPortal>& portals = level.Portals();

    std::vector<uint8_t> status(portals.size(), 0);
    for (size_t portal = 0; portal < portals.size(); portal++) {
        status[portal] = static_cast<uint8_t>(level.PortalCrossing(portal, from, to));
    }

    std::array<uint8_t, 16> source;
    std::array<uint8_t, 16> found;
    std::array<uint8_t, 16> all;
    source.fill(kNoRoom);
    found.fill(kNoRoom);
    all.fill(kNoRoom);
    for (size_t i = 0; i < 8; i++) {
        source[i] = fromRooms[i];
        all[i] = fromRooms[i];
    }

    const auto add = [](std::array<uint8_t, 16>& set, uint8_t room) {
        size_t i = 0;
        while (i < 0x10 && set[i] != kNoRoom) {
            if (room == set[i]) {
                return;
            }
            i++;
        }
        if (i < 0xF) {
            set[i] = room;
            set[i + 1] = kNoRoom;
        }
    };

    do {
        found[0] = kNoRoom;
        for (size_t portal = 0; portal < portals.size(); portal++) {
            for (size_t i = 0; i < 16 && source[i] != kNoRoom; i++) {
                if (status[portal] == 1 && portals[portal].room1 == source[i]) {
                    add(found, portals[portal].room2);
                    add(all, portals[portal].room2);
                    status[portal] = 0;
                }
                if (status[portal] == 2 && portals[portal].room2 == source[i]) {
                    add(found, portals[portal].room1);
                    add(all, portals[portal].room1);
                    status[portal] = 0;
                }
            }
        }
        if (found[0] == kNoRoom) {
            break;
        }
        source = found;
    } while (found[0] != kNoRoom);

    size_t count = 0;
    for (; count < 7 && source[count] != kNoRoom; count++) {
        (*finalRooms)[count] = source[count];
    }
    (*finalRooms)[count] = kNoRoom;

    traversed->clear();
    for (int i = 0; i < maxTraversed && all[static_cast<size_t>(i)] != kNoRoom; i++) {
        traversed->push_back(all[static_cast<size_t>(i)]);
    }
}

// -- Moving a flying object -----------------------------------------------------------------

int Projectiles::Move(PlacedObject& object, const Vec3& target, Vec3* hitPos, Vec3* hitNormal, bool canEmbed,
                      bool onlyIfPathClear) {
    // sub_GAME_7F042EB4.
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0 &&
        (shots_->Scene().objects->ProjectileAt(object.projectile).flags & ProjectileFlag::Sticky) != 0) {
        return MoveProjectile(object, target, hitPos, hitNormal, canEmbed, onlyIfPathClear);
    }
    return MoveObject(object, target, hitPos, hitNormal);
}

int Projectiles::MoveProjectile(PlacedObject& object, const Vec3& target, Vec3* hitPos, Vec3* hitNormal, bool canEmbed,
                                bool onlyIfPathClear) {
    // handles_projectile_motion.
    const ShotScene& scene = shots_->Scene();
    Objects& objects = *scene.objects;
    PropList& props = *scene.props;
    Prop& prop = props[object.prop];
    contact_ = ProjectileContact{};
    int result = 1;

    if (object.runtimePosition == target) {
        return result;
    }
    Vec3 endpos = target;
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) == 0 || object.projectile < 0) {
        return result;
    }
    Projectile& projectile = objects.ProjectileAt(object.projectile);
    if ((projectile.flags & ProjectileFlag::Sticky) == 0) {
        return result;
    }

    std::array<uint8_t, 8> roomSet{};
    std::vector<int> roomNums;
    FindRooms(object.runtimePosition, endpos, projectile.rooms, &roomSet, &roomNums, 20);

    // The rooms of the bg tests: those the segment goes through, then the rooms
    // the stage's commands made visible.
    const std::vector<uint8_t>& visible = scene.visibility->CommandRooms();
    const size_t visibleCount = std::min<size_t>(visible.size(), 100);
    for (size_t i = 0; i < visibleCount; i++) {
        roomNums.push_back(visible[i]);
    }
    std::vector<int> roomList = roomNums;
    roomList.push_back(-1);

    const float scale = 1.0f / scene.level->Level().scale;
    const Vec3 current = object.runtimePosition;
    for (const int room : roomNums) {
        HitThing hit;
        if (!scene.bgHit->TestBullet(current, endpos, room, &hit, scene.level->Room(room).geometry)) {
            continue;
        }
        for (float& axis : hit.hitpos) {
            axis *= scale;
        }
        const auto between = [&](int a) {
            const size_t i = static_cast<size_t>(a);
            return (current[i] <= endpos[i] && hit.hitpos[i] <= endpos[i] && current[i] <= hit.hitpos[i]) ||
                   (endpos[i] <= current[i] && endpos[i] <= hit.hitpos[i] && hit.hitpos[i] <= current[i]);
        };
        if (between(0) && between(1) && between(2)) {
            if (!(current[0] == hit.hitpos[0] && current[1] == hit.hitpos[1] && current[2] == hit.hitpos[2])) {
                result = 0;
                *hitPos = hit.hitpos;
                *hitNormal = hit.normal;
                if ((*hitNormal)[0] != 0.0f || (*hitNormal)[1] != 0.0f || (*hitNormal)[2] != 0.0f) {
                    const float length = Length(*hitNormal);
                    *hitNormal = { (*hitNormal)[0] / length, (*hitNormal)[1] / length, (*hitNormal)[2] / length };
                } else {
                    (*hitNormal)[2] = 1.0f;
                }
            }
        }
    }

    if (result == 0) {
        const Vec3 diff = { target[0] - object.runtimePosition[0], target[1] - object.runtimePosition[1],
                            target[2] - object.runtimePosition[2] };
        const float dist = Length(diff);
        const float factor = 0.1f < dist ? 0.1f / dist : 0.5f;
        for (size_t i = 0; i < 3; i++) {
            (*hitPos)[i] -= factor * diff[i];
        }
        endpos = *hitPos;
    }

    if (!shots_->FindCollidingProp(object.prop, object.runtimePosition, endpos, CdType::All & ~CdType::Bg,
                                   roomList.data(), hitPos, hitNormal, &contact_)) {
        if (result == 0 && canEmbed) {
            result = 2;
            if (!onlyIfPathClear) {
                FindRooms(object.runtimePosition, *hitPos, projectile.rooms, &roomSet, &roomNums, 20);
            }
        } else if (result == 1 || !onlyIfPathClear) {
            if (result != 1) {
                FindRooms(object.runtimePosition, endpos, projectile.rooms, &roomSet, &roomNums, 20);
            }
            object.runtimePosition[0] = endpos[0];
            object.runtimePosition[2] = endpos[2];
            prop.position[1] = endpos[1];
            object.runtimePosition[1] = endpos[1];
        }
    } else if (!onlyIfPathClear) {
        endpos[0] = object.runtimePosition[0];
        endpos[2] = object.runtimePosition[2];
        FindRooms(object.runtimePosition, endpos, projectile.rooms, &roomSet, &roomNums, 20);
        prop.position[1] = endpos[1];
        object.runtimePosition[1] = endpos[1];
        result = canEmbed ? 2 : 0;
    }

    if (result != 1 && onlyIfPathClear) {
        return result;
    }

    if ((projectile.flags & ProjectileFlag::OffTiles) == 0) {
        int tile = prop.stan;
        if (scene.stan->WalkTiles(&tile, prop.position[0], prop.position[2], object.runtimePosition[0],
                                  object.runtimePosition[2]) &&
            tile != kNoTile) {
            prop.stan = tile;
            prop.position[0] = object.runtimePosition[0];
            prop.position[2] = object.runtimePosition[2];
        } else {
            projectile.flags |= ProjectileFlag::OffTiles;
            prop.flags |= PropFlag::SeedFromRooms;
        }
    }

    if ((projectile.flags & ProjectileFlag::OffTiles) != 0) {
        float y = 0.0f;
        const int tile = scene.stan->FindTileBelow(object.runtimePosition, roomSet.data(), &y);
        if (tile != kNoTile) {
            prop.stan = tile;
            prop.position[0] = object.runtimePosition[0];
            prop.position[2] = object.runtimePosition[2];
            projectile.flags &= ~ProjectileFlag::OffTiles;
            prop.flags &= static_cast<uint8_t>(~PropFlag::SeedFromRooms);
        }
    }

    if (roomSet[0] != kNoRoom) {
        size_t i = 0;
        do {
            projectile.rooms[i] = roomSet[i];
            i++;
        } while (roomSet[i] != kNoRoom && i != 7);
        projectile.rooms[i] = kNoRoom;
    } else {
        projectile.rooms[0] = kNoRoom;
    }
    return result;
}

int Projectiles::MoveObject(PlacedObject& object, const Vec3& target, Vec3* hitPos, Vec3* hitNormal) {
    // objTryMovePropWithCollision.
    const ShotScene& scene = shots_->Scene();
    Stan& stan = *scene.stan;
    Prop& prop = (*scene.props)[object.prop];
    contact_ = ProjectileContact{};
    int tile = prop.stan;
    const float width = ObjectWidth(object);

    if (prop.position == target) {
        return 1;
    }
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) == 0 || object.projectile < 0) {
        return 1;
    }
    const Projectile& projectile = scene.objects->ProjectileAt(object.projectile);

    float ymax = 0.0f;
    float ymin = 1.0f;
    if ((projectile.flags & ProjectileFlag::Thrown) != 0) {
        const float floor = stan.PositionY(prop.stan, prop.position[0], prop.position[2]);
        if (target[1] < prop.position[1]) {
            ymax = prop.position[1] - floor;
            ymin = target[1] - floor;
        } else {
            ymax = target[1] - floor;
            ymin = prop.position[1] - floor;
        }
    }

    stan.ResetHits();
    if (stan.TestLineUnobstructed(&tile, prop.position[0], prop.position[2], target[0], target[2], kMoveCdTypes, ymax,
                                  ymin, 0.0f, 1.0f) != 0 &&
        tile != kNoTile) {
        if (stan.TestVolume(&tile, target[0], target[2], width, 0x1f, ymax, ymin) < 0) {
            object.runtimePosition[0] = target[0];
            object.runtimePosition[2] = target[2];
            prop.stan = tile;
            prop.position[0] = target[0];
            prop.position[2] = target[2];
            prop.position[1] = target[1];
            object.runtimePosition[1] = target[1];
            return 1;
        }
    }

    Vec3 edgeA{};
    Vec3 edgeB{};
    stan.CollisionEdge(&edgeA, &edgeB);
    Vec3 normal = { edgeB[2] - edgeA[2], 0.0f, edgeA[0] - edgeB[0] };
    if (normal[0] != 0.0f || normal[2] != 0.0f) {
        const float length = Length(normal);
        normal = { normal[0] / length, normal[1] / length, normal[2] / length };
    } else {
        normal[2] = 1.0f;
    }
    *hitNormal = normal;

    Vec3 partial{};
    if (target != prop.position) {
        const Vec3 direction = { target[0] - prop.position[0], target[1] - prop.position[1],
                                 target[2] - prop.position[2] };
        *hitPos = ChrActions::LineLineIntersection(edgeA, edgeB, prop.position, direction);

        const Vec2 edgeStart = { edgeA[0], edgeA[2] };
        const Vec2 edgeEnd = { edgeB[0], edgeB[2] };
        const Vec2 direction2d = { target[0] - prop.position[0], target[2] - prop.position[2] };
        const float fraction = CircleSegmentContactFraction(width, { prop.position[0], prop.position[2] }, edgeStart,
                                                            edgeEnd, direction2d);

        tile = prop.stan;
        partial = { prop.position[0] + ((direction2d[0] * fraction) * 0.99000001f), target[1],
                    prop.position[2] + ((direction2d[1] * fraction) * 0.99000001f) };
        if (stan.TestLineUnobstructed(&tile, prop.position[0], prop.position[2], partial[0], partial[2], kMoveCdTypes,
                                      ymax, ymin, 0.0f, 1.0f) != 0 &&
            tile != kNoTile) {
            if (stan.TestVolume(&tile, partial[0], partial[2], width, kMoveCdTypes, ymax, ymin) < 0) {
                object.runtimePosition[0] = partial[0];
                object.runtimePosition[2] = partial[2];
                prop.stan = tile;
                prop.position[0] = partial[0];
                prop.position[2] = partial[2];
            }
        }
    } else {
        *hitPos = target;
    }
    prop.position[1] = partial[1];
    object.runtimePosition[1] = partial[1];
    return 0;
}

// -- Coming to rest -------------------------------------------------------------------------

void Projectiles::Settle(PlacedObject& object, const Vec3& previousXAxis) {
    // objSettle.
    Objects& objects = *shots_->Scene().objects;
    object.runtimeFlags &= ~ObjectRuntime::FellDestroyed;
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) == 0 || object.projectile < 0) {
        return;
    }
    Projectile& projectile = objects.ProjectileAt(object.projectile);

    if (object.type == static_cast<uint8_t>(PropDefType::Door)) {
        objects.FreeProjectile(object.projectile);
        object.projectile = -1;
        object.runtimeFlags &= ~ObjectRuntime::HasProjectile;
        return;
    }

    projectile.ownerProp = -1;
    projectile.flags &= ~ProjectileFlag::Airborne;
    projectile.flags &= ~ProjectileFlag::Sticky;

    Vec3 angles = RotationAngles(object.mtx);
    const Mtxf rotation = RotationXYZ(angles);
    projectile.settleFrom = ToArray(QuaternionOf(angles));

    Mtxf aim = InverseRotation(rotation);
    const Mtxf scaleMatrix = Multiply(aim, object.mtx);
    projectile.scaleX =
        std::sqrt((scaleMatrix.m[0][0] * scaleMatrix.m[0][0]) + (scaleMatrix.m[0][1] * scaleMatrix.m[0][1]) +
                  (scaleMatrix.m[0][2] * scaleMatrix.m[0][2]));
    projectile.scaleY =
        std::sqrt((scaleMatrix.m[1][0] * scaleMatrix.m[1][0]) + (scaleMatrix.m[1][1] * scaleMatrix.m[1][1]) +
                  (scaleMatrix.m[1][2] * scaleMatrix.m[1][2]));
    projectile.scaleZ =
        std::sqrt((scaleMatrix.m[2][0] * scaleMatrix.m[2][0]) + (scaleMatrix.m[2][1] * scaleMatrix.m[2][1]) +
                  (scaleMatrix.m[2][2] * scaleMatrix.m[2][2]));

    float x = object.mtx.m[0][0];
    float z = object.mtx.m[0][2];
    if (x != 0.0f || z != 0.0f) {
        const float inverse = 1.0f / std::sqrt((x * x) + (z * z));
        x *= inverse;
        z *= inverse;
    } else {
        z = 1.0f;
        x = 0.0f;
    }

    aim.m[0][0] = x;
    aim.m[0][1] = 0.0f;
    aim.m[0][2] = z;
    aim.m[0][3] = 0.0f;
    if (object.mtx.m[1][1] >= 0.0f) {
        aim.m[1][0] = 0.0f;
        aim.m[1][1] = 1.0f;
        aim.m[1][2] = 0.0f;
        aim.m[1][3] = 0.0f;
        aim.m[2][0] = -z;
        aim.m[2][1] = 0.0f;
        aim.m[2][2] = x;
        aim.m[2][3] = 0.0f;
    } else {
        aim.m[1][0] = 0.0f;
        aim.m[1][1] = -1.0f;
        aim.m[1][2] = 0.0f;
        aim.m[1][3] = 0.0f;
        aim.m[2][0] = z;
        aim.m[2][1] = 0.0f;
        aim.m[2][2] = -x;
        aim.m[2][3] = 0.0f;
    }
    aim.m[3][0] = 0.0f;
    aim.m[3][1] = 0.0f;
    aim.m[3][2] = 0.0f;
    aim.m[3][3] = 1.0f;

    angles = RotationAngles(aim);
    projectile.settleTo = ToArray(QuaternionOf(angles));
    projectile.settleTo = ToArray(EnsureShortestPath(FromArray(projectile.settleFrom), FromArray(projectile.settleTo)));
    projectile.settleProgress = 0.0f;

    const float angle = GameAcosf(((aim.m[0][0] * rotation.m[0][0]) + (aim.m[0][1] * rotation.m[0][1])) +
                                  (aim.m[0][2] * rotation.m[0][2]));

    if ((angle > 0.0f && object.mtx.m[0][1] > 0.0f) && previousXAxis[1] < object.mtx.m[0][1]) {
        projectile.settleRate = 0.050000001f / ((angle * 4.0f) / kTau);
    } else if ((angle > 0.0f && object.mtx.m[0][1] < 0.0f) && object.mtx.m[0][1] < previousXAxis[1]) {
        projectile.settleRate = 0.050000001f / ((angle * 4.0f) / kTau);
    } else {
        const float modelScale = object.scale;
        float turn = GameAcosf((((previousXAxis[0] * object.mtx.m[0][0]) + (previousXAxis[1] * object.mtx.m[0][1])) +
                                (object.mtx.m[0][2] * previousXAxis[2])) /
                               (modelScale * modelScale));
        turn = turn / clock_.globalTimerDelta;
        projectile.settleRate = angle != 0.0f ? turn / angle : 1.0f;
    }

    if (projectile.settleRate < 0.0f) {
        projectile.settleRate = -projectile.settleRate;
    }
    if (projectile.settleRate < 0.029999999f) {
        projectile.settleRate = 0.029999999f;
    } else if (0.15000001f < projectile.settleRate) {
        projectile.settleRate = 0.15000001f;
    }
}

// -- Sticking to things ---------------------------------------------------------------------

void Projectiles::StickToSurface(PlacedObject& object, const Vec3& point, int stan, const Vec3& normal) {
    // sub_GAME_7F0439B8.
    Objects& objects = *shots_->Scene().objects;
    Mtxf matrix = SurfaceMatrix(normal);
    ScaleRows(&matrix, object.scale);
    objects.SetPosition(&object, point, matrix, stan);

    const ModelBounds* bounds = objects.BoundsOf(object);
    const float ymin = bounds != nullptr ? bounds->ymin : 0.0f;
    for (size_t axis = 0; axis < 3; axis++) {
        object.runtimePosition[axis] -= ymin * object.mtx.m[1][axis];
    }
    objects.UpdateCollision(&object);
}

void Projectiles::LandingDamage(PlacedObject& object) {
    const ShotScene& scene = shots_->Scene();
    const int owner = static_cast<int>((object.runtimeFlags & ObjectRuntime::OwnerMask) >> ObjectRuntime::OwnerShift);
    const float damage = (static_cast<float>(scene.random()) * kFraction * 4.0f) + 2.0f;
    scene.damage->Apply(object, damage, (*scene.props)[object.prop].position, kItemUnarmed, owner);
}

void Projectiles::EndTick() {
    if (hooks_.backgroundGuards) {
        hooks_.backgroundGuards([this](const Vec3& position) { DetonateProximityMines(position); });
    }
    remoteMineTrigger_ = 0;
}

void Projectiles::InitObject(int propId, const Vec3& position, int stan, Mtxf matrix, const Vec3& velocity,
                             const Mtxf& spin, int ownerProp) {
    // gunInitProjectileObject.
    const ShotScene& scene = shots_->Scene();
    Objects& objects = *scene.objects;
    PlacedObject* object = objects.ForProp(propId);
    if (object == nullptr || object->prop < 0) {
        return;
    }
    scene.props->Activate(propId);
    scene.props->Enable(propId);
    ScaleRows(&matrix, object->scale);
    objects.SetPosition(object, position, matrix, stan);
    objects.UpdateRooms(object);
    objects.UpdateCollision(object);
    objects.GiveProjectile(propId);

    if ((object->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object->projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(object->projectile);
        projectile.flags |= ProjectileFlag::Airborne | ProjectileFlag::Thrown;
        projectile.ownerProp = ownerProp;
        objects.SetSticky(propId);
        projectile.spin = spin;
        projectile.speed = velocity;
        projectile.prop = propId;
        projectile.created = clock_.frame;
    }
}

void Projectiles::InitFromPlayer(int propId, const Vec3& target, const Mtxf& matrix, const Vec3& velocity,
                                 const Mtxf& spin, int playerProp, float standHeight, const Vec3& viewerPosition,
                                 int playerRoom) {
    // gunInitProjectileFromPlayer.
    const ShotScene& scene = shots_->Scene();
    Objects& objects = *scene.objects;
    PlacedObject* object = objects.ForProp(propId);
    if (object == nullptr || object->prop < 0) {
        return;
    }
    const Prop& player = (*scene.props)[playerProp];

    float high = 0.0f;
    float low = 0.0f;
    if (target[1] < player.position[1]) {
        high = player.position[1] - standHeight;
        low = target[1] - standHeight;
    } else {
        high = target[1] - standHeight;
        low = player.position[1] - standHeight;
    }

    int tile = player.stan;
    Vec3 position{};
    bool usedFallback = false;
    if (scene.stan->TestLineUnobstructed(&tile, player.position[0], player.position[2], target[0], target[2], 0x1f,
                                         high, low, 0.0f, 1.0f) != 0) {
        position = target;
    } else {
        tile = player.stan;
        position = player.position;
        usedFallback = true;
    }

    InitObject(propId, position, tile, matrix, velocity, spin, playerProp);

    if ((object->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object->projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(object->projectile);
        if (usedFallback) {
            projectile.flags |= ProjectileFlag::Blocked;
            projectile.launchTarget = target;
        }
        std::array<uint8_t, 8> rooms{};
        rooms.fill(kNoRoom);
        rooms[0] = static_cast<uint8_t>(playerRoom);
        std::vector<int> traversed;
        FindRooms(viewerPosition, position, rooms, &projectile.rooms, &traversed, 0x14);
    }
}

bool Projectiles::Embed(int propId, int parentProp, const Model::ModelInstance* model, int32_t node) {
    // objEmbed.
    const ShotScene& scene = shots_->Scene();
    PropList& props = *scene.props;
    if ((props[parentProp].flags & PropFlag::OnScreen) == 0) {
        return false;
    }
    PlacedObject* object = scene.objects->ForProp(propId);
    if (object == nullptr || model == nullptr || node == kNoNode) {
        return false;
    }
    if (!scene.objects->AllocateEmbedment()) {
        return false;
    }
    object->hasEmbedment = true;
    object->embedmentProjectile = -1;

    const int matrixIndex = model->MatrixIndexOfNode(node);
    Mtxf nodeMatrix = IdentityMtxf();
    if (matrixIndex >= 0 && static_cast<size_t>(matrixIndex) < model->Walk().matrices.size()) {
        std::memcpy(nodeMatrix.m, model->Walk().matrices[static_cast<size_t>(matrixIndex)].m, sizeof(nodeMatrix.m));
    }

    object->runtimeFlags |= ObjectRuntime::Embedded;
    props.DeregisterRooms(propId);
    props.Delist(propId);
    props.Disable(propId);
    object->embedNode = node;
    props.Reparent(propId, parentProp);

    Mtxf placed = object->mtx;
    for (size_t axis = 0; axis < 3; axis++) {
        placed.m[3][axis] = object->runtimePosition[axis];
    }
    Mtxf viewToWorld = scene.camera->ViewToWorld();
    const Mtxf nodeWorld = MultiplyHomogeneous(viewToWorld, nodeMatrix);
    const Mtxf inverse = InvertAffine(nodeWorld);
    object->embedment = MultiplyHomogeneous(inverse, placed);
    return true;
}

} // namespace Frigate::Level
