#pragma once

#include <array>
#include <cstdint>

#include "level/BgHeader.h"
#include "level/Matrix.h"
#include "level/ModelHandle.h"
#include "level/PropModels.h"
#include "level/StanIntersection.h"

namespace Frigate::Level {

// TANK_RUN_STATE_*.
enum TankRunState : int32_t {
    kTankNotRunning,
    kTankStarting,
    kTankRunning,
};

// US values of the constants bondview2.c defines for the tank.
constexpr float kTankMaxSpeed = 15.0f;
constexpr float kTankRotationScale = 0.92f;
constexpr float kTankTurretVerticalScale = 0.94f;
constexpr float kTankHeightScale = 0.83f;
constexpr int kTankDamagePenaltyTicks = 90;
constexpr float kTankVertAngleFactor = 0.0600000023842f;
constexpr float kTankVertAngleRadFactor = 0.0799999833107f;
// g_TankShellSpeed.
constexpr float kTankShellSpeed = 66.666664f;

// The runtime fields of a TankRecord that the tank's movement, pose and
// collision share.
struct TankState {
    // The footprint of the top of the tank the player stands on (rect and the
    // edge count the record's collision field holds), refreshed when the tank
    // is posed.
    std::array<Vec2, 8> rect{};
    int rectEdges = 0;
    // is_firing_tank.
    bool firing = false;
    // turret_vertical_angle, turret_orientation_angle and
    // tank_orientation_angle, in radians.
    float turretVerticalAngle = 0.0f;
    float turretOrientationAngle = 0.0f;
    float tankOrientationAngle = 0.0f;
    // unkD0: the smoothed floor height the tank rides at, and stan_y the floor
    // height under it.
    float filteredHeight = 0.0f;
    float stanY = 0.0f;
    // unkD8: the shells the tank holds while the player is outside it.
    int32_t ammo = 0;
};

// bondviewTankModelRotationRelated: where the seat is from the tank's centre,
// in model units, given the turret's heading.
Vec3 TankModelOffset(const TankGeometry& geometry, float turretOrientation);

// bondviewGetTankCollisionBounds: the corners of the hull (`hull` is the
// model's bounding box) with the seat at `position`, as a four-sided polygon.
std::array<Vec2, 8> TankCollisionBounds(const ModelBounds& hull, const Vec3& modelOffset, float scale,
                                        const Vec3& position, float orientation);

// The matrices the tank model is posed with under `base` (the matrix its body
// is drawn with): index 1 turns the turret, 3 pitches the gun, 4 is the
// muzzle and 2 the seat (propobj.c's PROPDEF_TANK pose). Index 0 is `base`.
std::array<Mtxf, 5> TankPose(const TankGeometry& geometry, const TankState& state, const Mtxf& base);

// sub_GAME_7F03F540 with the turret's matrix: the footprint of the top box.
void UpdateTankRect(const TankGeometry& geometry, TankState* state, const Mtxf& worldBase);

} // namespace Frigate::Level
