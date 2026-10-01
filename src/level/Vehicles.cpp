#include "level/Vehicles.h"

#include <algorithm>
#include <cmath>

#include "level/ApplySpeed.h"
#include "level/ChrActions.h"
#include "level/Matrix.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;

// TRUCK_TURN_ACCEL_PER_FRAME, TRUCK_TURN_DECEL_PER_FRAME and TRUCK_TURN_MAX_SPEED.
constexpr float kTurnAccel = 0.0001308997f;
constexpr float kTurnDecel = 0.0002617994f;
constexpr float kTurnMaxSpeed = 0.007853982f;

// How far from a waypoint a vehicle counts as having passed it, and the
// distance ahead within which it stops steering for it.
constexpr float kPassRange = 100.0f;
constexpr float kAheadRange = 10.0f;
// The radius of the circle a vehicle's new position is tested with.
constexpr float kBodyRadius = 10.0f;
// The ticks a halted vehicle takes to pick its speed back up, and a finished
// route to bring it to a stop.
constexpr float kRestartTicks = 60.0f;
constexpr int kTested = 0x1F;

Mtxf ScaledYaw(float yaw, float scale) {
    Mtxf out = RotationY(yaw);
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            out.m[row][column] *= scale;
        }
    }
    return out;
}

// matrix_4x4_transform_vector.
Vec3 Transform(const Mtxf& matrix, const std::array<float, 3>& v) {
    Vec3 out{};
    for (int i = 0; i < 3; i++) {
        out[static_cast<size_t>(i)] = (matrix.m[0][i] * v[0]) + (matrix.m[1][i] * v[1]) + (matrix.m[2][i] * v[2]) +
                                      matrix.m[3][i];
    }
    return out;
}

void Normalize(Vec3* v) {
    // guNormalize.
    const float length = std::sqrt(((*v)[0] * (*v)[0]) + ((*v)[1] * (*v)[1]) + ((*v)[2] * (*v)[2]));
    if (length != 0.0f) {
        const float scale = 1.0f / length;
        (*v)[0] *= scale;
        (*v)[1] *= scale;
        (*v)[2] *= scale;
    }
}

} // namespace

void Vehicles::Tick(PlacedObject& object) {
    if (object.type == static_cast<uint8_t>(PropDefType::Vehicle)) {
        TickVehicle(object);
    } else if (object.type == static_cast<uint8_t>(PropDefType::Aircraft)) {
        TickAircraft(object);
    }
}

const VehicleGeometry* Vehicles::Geometry(const PlacedObject& object) {
    const PropModel* model = objects_.Model(object.model);
    if (model == nullptr || models_ == nullptr) {
        return nullptr;
    }
    auto found = geometry_.find(model->name);
    if (found == geometry_.end()) {
        found = geometry_.emplace(model->name, models_->LoadVehicleGeometry("models/prop/P" + model->name + "Z")).first;
    }
    return found->second ? &*found->second : nullptr;
}

void Vehicles::RampSpeed(PlacedObject& object, float delta) {
    ObjectAi& ai = object.ai;
    if (ai.speedTime60 >= 0.0f) {
        if (ai.speedTime60 <= delta) {
            ai.speed = ai.speedAim;
        } else {
            ai.speed += ((ai.speedAim - ai.speed) * delta) / ai.speedTime60;
        }
        ai.speedTime60 -= delta;
    }
}

bool Vehicles::Settle(PlacedObject& object) {
    const VehicleGeometry* geometry = Geometry(object);
    Prop& prop = objects_.Props()[object.prop];
    if (geometry == nullptr) {
        return false;
    }
    const float scale = object.scale;
    const float yaw = object.ai.yaw;
    bool onTiles = true;

    const Mtxf placement = [&] {
        Mtxf mtx = ScaledYaw(yaw, scale);
        for (size_t i = 0; i < 3; i++) {
            mtx.m[3][i] = object.runtimePosition[i];
        }
        return mtx;
    }();

    std::array<Vec3, 4> points;
    for (size_t i = 0; i < points.size(); i++) {
        points[i] = Transform(placement, geometry->points[i]);
        int tile = prop.stan;
        if (stan_.WalkTiles(&tile, prop.position[0], prop.position[2], points[i][0], points[i][2])) {
            points[i][1] = stan_.PositionY(tile, points[i][0], points[i][2]);
        } else {
            onTiles = false;
        }
    }

    if (onTiles) {
        Vec3 edge0 = { points[1][0] - points[0][0], points[1][1] - points[0][1], points[1][2] - points[0][2] };
        Normalize(&edge0);
        Vec3 edge1 = { points[3][0] - points[1][0], points[3][1] - points[1][1], points[3][2] - points[1][2] };
        Normalize(&edge1);
        Vec3 edge2 = { points[2][0] - points[3][0], points[2][1] - points[3][1], points[2][2] - points[3][2] };
        Normalize(&edge2);
        Vec3 edge3 = { points[0][0] - points[2][0], points[0][1] - points[2][1], points[0][2] - points[2][2] };
        Normalize(&edge3);

        const float cross0y = (edge0[2] * edge1[0]) - (edge1[2] * edge0[0]);
        const float cross1y = (edge1[2] * edge2[0]) - (edge2[2] * edge1[0]);
        const float cross2y = (edge2[2] * edge3[0]) - (edge3[2] * edge2[0]);
        const float cross3y = (edge3[2] * edge0[0]) - (edge0[2] * edge3[0]);

        // The normal of the steepest-facing corner of the four points.
        Vec3 normal{};
        if (cross1y <= cross0y && cross2y <= cross0y && cross3y <= cross0y) {
            normal = { (edge0[1] * edge1[2]) - (edge1[1] * edge0[2]), cross0y,
                       (edge0[0] * edge1[1]) - (edge1[0] * edge0[1]) };
        } else if (cross2y <= cross1y && cross3y <= cross1y) {
            normal = { (edge1[1] * edge2[2]) - (edge2[1] * edge1[2]), cross1y,
                       (edge1[0] * edge2[1]) - (edge2[0] * edge1[1]) };
        } else if (cross3y <= cross2y) {
            normal = { (edge2[1] * edge3[2]) - (edge3[1] * edge2[2]), cross2y,
                       (edge2[0] * edge3[1]) - (edge3[0] * edge2[1]) };
        } else {
            normal = { (edge3[1] * edge0[2]) - (edge0[1] * edge3[2]), cross3y,
                       (edge3[0] * edge0[1]) - (edge0[0] * edge3[1]) };
        }

        const Vec3 yawVector = { std::sin(yaw), 0.0f, std::cos(yaw) };

        Mtxf& mtx = object.mtx;
        mtx = IdentityMtxf();
        mtx.m[1][0] = normal[0];
        mtx.m[1][1] = normal[1];
        mtx.m[1][2] = normal[2];
        mtx.m[0][0] = (mtx.m[1][1] * yawVector[2]) - (mtx.m[1][2] * yawVector[1]);
        mtx.m[0][1] = (mtx.m[1][2] * yawVector[0]) - (mtx.m[1][0] * yawVector[2]);
        mtx.m[0][2] = (mtx.m[1][0] * yawVector[1]) - (mtx.m[1][1] * yawVector[0]);
        mtx.m[2][0] = (mtx.m[0][1] * mtx.m[1][2]) - (mtx.m[0][2] * mtx.m[1][1]);
        mtx.m[2][1] = (mtx.m[0][2] * mtx.m[1][0]) - (mtx.m[0][0] * mtx.m[1][2]);
        mtx.m[2][2] = (mtx.m[0][0] * mtx.m[1][1]) - (mtx.m[0][1] * mtx.m[1][0]);
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                mtx.m[row][column] *= scale;
            }
        }

        const Vec3 raised = Transform(mtx, geometry->points[0]);
        // chrpropBBOXGetYmin of the hull box.
        const float ypos = (points[0][1] - (geometry->hull[2] * scale)) - raised[1];
        prop.position[1] = ypos;
        object.runtimePosition[1] = ypos;
    } else {
        object.mtx = ScaledYaw(yaw, scale);
        const float ypos =
            stan_.PositionY(prop.stan, prop.position[0], prop.position[2]) -
            ((geometry->hull[2] + geometry->points[0][1]) * scale);
        prop.position[1] = ypos;
        object.runtimePosition[1] = ypos;
    }
    return onTiles;
}

void Vehicles::TickVehicle(PlacedObject& object) {
    PropList& props = objects_.Props();
    Prop& prop = props[object.prop];
    ObjectAi& ai = object.ai;
    const ChrTiming& timing = chrs_.Timing();
    const float delta = timing.globalTimerDelta;
    const VehicleGeometry* geometry = Geometry(object);

    float targetYaw = ai.yaw;
    const Vec3* waypoint = nullptr;

    ai_.RunObject(object);

    RampSpeed(object, delta);

    // TODO(https://github.com/liz-baker/frigate/issues/41): the engine's sound
    // while the vehicle drives and is within 3000 units of the player.

    if (ai.path >= 0) {
        const PatrolPath& path = paths_.Paths()[static_cast<size_t>(ai.path)];
        const size_t step = std::min(static_cast<size_t>(std::max(ai.nextStep, 0)), path.waypoints.size() - 1);
        waypoint = &paths_.PadOf(path.waypoints[step]).position;
        targetYaw = std::atan2((*waypoint)[0] - object.runtimePosition[0], (*waypoint)[2] - object.runtimePosition[2]);
        if ((object.flags & ObjectFlag::InMotion) != 0) {
            ai.yaw = targetYaw;
            object.flags &= ~ObjectFlag::InMotion;
            Settle(object);
        }
    } else if ((object.flags & ObjectFlag::InMotion) != 0) {
        ai.yaw = std::atan2(object.mtx.m[2][0], object.mtx.m[2][2]);
        object.flags &= ~ObjectFlag::InMotion;
        Settle(object);
    }

    if (ai.speed > 0.0f) {
        if (geometry == nullptr) {
            return;
        }
        int currentTile = prop.stan;
        const float previousYaw = ai.yaw;
        const float previousTurn = ai.turnRate;
        if (waypoint != nullptr) {
            const Vec3 forward = { std::sin(ai.yaw), 0.0f, std::cos(ai.yaw) };
            if (ChrActions::WithinRangeAhead(object.runtimePosition, forward, *waypoint, kAheadRange)) {
                targetYaw = ai.yaw;
            }
        }

        ChrObjCallsApplySpeed(&ai.yaw, targetYaw, &ai.turnRate, kTurnAccel, kTurnDecel, kTurnMaxSpeed, timing.clockTimer);
        while (ai.yaw >= kTau) {
            ai.yaw -= kTau;
        }
        while (ai.yaw < 0.0f) {
            ai.yaw += kTau;
        }

        if (targetYaw == ai.yaw && ai.turnRate <= kTurnDecel && ai.turnRate >= -kTurnDecel) {
            ai.turnRate = 0.0f;
        }

        float angularVelocity = 0.0f;
        if (delta > 0.0f) {
            angularVelocity = (ai.yaw - previousYaw) / delta;
        }
        if (angularVelocity < 0.0f) {
            angularVelocity += kTau;
        }
        // The rear axle swings the vehicle's centre as it turns.
        const float swing = ((geometry->points[2][2] * object.scale) * std::sin(angularVelocity)) * delta;
        const Vec3 forward = { std::sin(ai.yaw), 0.0f, std::cos(ai.yaw) };
        const Vec3 next = { (object.runtimePosition[0] + ((ai.speed * delta) * forward[0])) - (forward[2] * swing),
                            object.runtimePosition[1],
                            (object.runtimePosition[2] + ((ai.speed * delta) * forward[2])) + (forward[0] * swing) };

        if (stan_.TestLineUnobstructed(&currentTile, prop.position[0], prop.position[2], next[0], next[2], kTested, 0.0f,
                                       1.0f, 0.0f, 1.0f) != 0 &&
            stan_.TestVolume(&currentTile, next[0], next[2], kBodyRadius, kTested, 0.0f, 1.0f) < 0) {
            const int previousTile = prop.stan;
            const Vec3 previous = prop.position;
            prop.stan = currentTile;
            object.runtimePosition[0] = prop.position[0] = next[0];
            object.runtimePosition[2] = prop.position[2] = next[2];
            objects_.Moved(&object);

            bool clear = objects_.FootprintClear(object.prop);
            if (clear) {
                // The vehicle's whole box must stay on the tiles.
                const float xmin = geometry->box[0];
                const float xmax = geometry->box[1];
                const float zmin = geometry->box[4];
                const float zmax = geometry->box[5];
                const Mtxf& mtx = object.mtx;
                const std::array<float, 2> a = { xmin * mtx.m[0][0], xmin * mtx.m[0][2] };
                const std::array<float, 2> b = { xmax * mtx.m[0][0], xmax * mtx.m[0][2] };
                const std::array<float, 2> c = { zmin * mtx.m[2][0], zmin * mtx.m[2][2] };
                const std::array<float, 2> d = { zmax * mtx.m[2][0], zmax * mtx.m[2][2] };
                const std::array<std::array<float, 2>, 4> corners = {
                    std::array<float, 2>{ prop.position[0] + a[0] + c[0], prop.position[2] + a[1] + c[1] },
                    std::array<float, 2>{ prop.position[0] + b[0] + c[0], prop.position[2] + b[1] + c[1] },
                    std::array<float, 2>{ prop.position[0] + b[0] + d[0], prop.position[2] + b[1] + d[1] },
                    std::array<float, 2>{ prop.position[0] + a[0] + d[0], prop.position[2] + a[1] + d[1] } };

                currentTile = prop.stan;
                float fromX = prop.position[0];
                float fromZ = prop.position[2];
                for (size_t i = 0; i <= corners.size() && clear; i++) {
                    const auto& to = corners[i % corners.size()];
                    if (!stan_.WalkTiles(&currentTile, fromX, fromZ, to[0], to[1])) {
                        clear = false;
                    }
                    fromX = to[0];
                    fromZ = to[1];
                }
            }

            if (clear) {
                Settle(object);
                objects_.UpdateShading(&object);
                // TODO(https://github.com/liz-baker/frigate/issues/136):
                // detonate_proxmine_In_range for the mines the vehicle drives over.
                if (waypoint != nullptr && ChrActions::ArrivingAt(previous, next, *waypoint, kPassRange)) {
                    ai.nextStep++;
                    const PatrolPath& path = paths_.Paths()[static_cast<size_t>(ai.path)];
                    if (static_cast<size_t>(ai.nextStep) >= path.waypoints.size() ||
                        path.waypoints[static_cast<size_t>(ai.nextStep)] < 0) {
                        ai.path = -1;
                        ai.speedAim = 0.0f;
                        ai.speedTime60 = kRestartTicks;
                    }
                }
            } else {
                if (ai.speedTime60 < 0.0f) {
                    ai.speedAim = ai.speed;
                    ai.speedTime60 = kRestartTicks;
                }
                ai.speed = 0.0f;
                ai.yaw = previousYaw;
                ai.turnRate = previousTurn;
                prop.stan = previousTile;
                object.runtimePosition = previous;
                prop.position = previous;
                objects_.Moved(&object);
            }
        } else {
            if (ai.speedTime60 < 0.0f) {
                ai.speedAim = ai.speed;
                ai.speedTime60 = kRestartTicks;
            }
            ai.speed = 0.0f;
            ai.yaw = previousYaw;
            ai.turnRate = previousTurn;
        }
    } else if ((object.flags & ObjectFlag::InMotion) != 0) {
        ai.yaw = std::atan2(object.mtx.m[2][0], object.mtx.m[2][2]);
        object.flags &= ~ObjectFlag::InMotion;
        Settle(object);
    }
}

void Vehicles::TickAircraft(PlacedObject& object) {
    ObjectAi& ai = object.ai;
    const float delta = chrs_.Timing().globalTimerDelta;

    ai_.RunObject(object);

    // TODO(https://github.com/liz-baker/frigate/issues/189): the aircraft's
    // animation (a model with a clip bound by AI_PlayAnimation moves the
    // aircraft along its root motion) and the rotor it turns.

    RampSpeed(object, delta);

    if (ai.rotorSpeedTime >= 0.0f) {
        if (ai.rotorSpeedTime <= delta) {
            ai.rotorSpeed = ai.rotorSpeedAim;
        } else {
            ai.rotorSpeed += ((ai.rotorSpeedAim - ai.rotorSpeed) * delta) / ai.rotorSpeedTime;
        }
        ai.rotorSpeedTime -= delta;
    }

    // TODO(https://github.com/liz-baker/frigate/issues/41): the helicopter's
    // engine sound while the rotor turns.
}

} // namespace Frigate::Level
