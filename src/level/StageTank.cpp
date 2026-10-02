#include "level/StageTank.h"

#include <algorithm>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
// CHRFLAG_01000000: the character was run over.
constexpr uint32_t kChrCrushed = 0x01000000;
// What a tank's run through an object does to it.
constexpr float kCrushDamage = 10000.0f;
constexpr float kRunOverDamage = 3.0f;

} // namespace

PlacedObject* Stage::TankBridge::TankOf(int prop) {
    PlacedObject* object = stage_.objects_.ForProp(prop);
    return object != nullptr && object->type == static_cast<uint8_t>(PropDefType::Tank) ? object : nullptr;
}

const TankGeometry* Stage::TankBridge::Geometry(const PlacedObject& tank) {
    return stage_.objects_.TankGeometryOf(tank);
}

const ModelBounds* Stage::TankBridge::Hull(const PlacedObject& tank) {
    return stage_.objects_.BoundsOf(tank);
}

bool Stage::TankBridge::CollisionBounds(int prop, PropCollider* out) {
    return stage_.objects_.CollisionBounds(prop, out);
}

void Stage::TankBridge::SetCollides(int prop, bool collides) {
    stage_.SetPropCollides(prop, collides);
}

void Stage::TankBridge::EnterTank(int32_t ammo) {
    stage_.intro_.inventory.AddItem(kItemTankShells);
    stage_.weapons_.AddAmmoToWeapon(kItemTankShells, ammo);
}

int32_t Stage::TankBridge::LeaveTank() {
    PlayerWeapons& weapons = stage_.weapons_;
    int32_t ammo = weapons.AmmoCountForWeapon(kItemTankShells);

    weapons.AddAmmoToWeapon(kItemTankShells, 0);
    stage_.intro_.inventory.RemoveItem(kItemTankShells);

    if (weapons.WeaponId(HandRight) == kItemTankShells) {
        ammo += weapons.AmmoInMagazine(HandRight);
        weapons.AutoAdvanceOnDepleteAllAmmo();
    }
    return ammo;
}

int Stage::TankBridge::TickTank(const Frame& frame) {
    Objects& objects = stage_.objects_;
    Stan& stan = stage_.level_.StanFile();
    PlacedObject& tank = *frame.tank;
    Prop& prop = objects.Props()[frame.prop];
    TankState& state = tank.tank;
    const TankGeometry* geometry = objects.TankGeometryOf(tank);
    const ModelBounds* hull = objects.BoundsOf(tank);
    int penalty = frame.damagePenaltyTicks;
    if (geometry == nullptr || hull == nullptr) {
        return penalty;
    }

    state.firing = frame.firing;
    state.turretVerticalAngle = frame.turretVerticalAngle;
    state.turretOrientationAngle = frame.turretOrientation;
    state.tankOrientationAngle = frame.tankOrientation;

    // The tank sits behind the player's seat, turned as the hull is.
    Mtxf rotation = RotationY(kTau - frame.tankOrientation);
    ScaleRows(&rotation, tank.scale);
    Vec3 position = RotateVector(rotation, { -frame.modelOffset[0], -frame.modelOffset[1], -frame.modelOffset[2] });
    position[0] += frame.collisionPosition[0];
    position[2] += frame.collisionPosition[2];

    prop.stan = frame.tile;
    stan.WalkTiles(&prop.stan, frame.collisionPosition[0], frame.collisionPosition[2], position[0], position[2]);

    state.stanY = stan.PositionY(prop.stan, position[0], position[2]);
    for (int i = 0; i < frame.clockTimer; i++) {
        state.filteredHeight = (state.filteredHeight * kTankHeightScale) + state.stanY;
    }
    position[1] = (state.filteredHeight * (1.0f - kTankHeightScale)) - (hull->ymin * tank.scale) + 4.0f;

    tank.mtx = rotation;
    tank.runtimePosition = position;
    prop.position = position;
    objects.Moved(&tank);

    const std::array<Vec2, 8> bounds =
        TankCollisionBounds(*hull, frame.modelOffset, tank.scale, frame.collisionPosition, frame.tankOrientation);

    // roomGetProps over the tank's rooms.
    std::array<int, 9> rooms{};
    size_t roomCount = 0;
    for (const uint8_t room : prop.rooms) {
        if (room == kNoRoom) {
            break;
        }
        rooms[roomCount++] = room;
    }
    rooms[roomCount] = -1;
    const std::vector<int16_t> nearby = objects.Props().RoomProps(rooms.data());

    for (const int16_t other : nearby) {
        if (other == frame.prop) {
            continue;
        }
        const Prop& neighbour = objects.Props()[other];
        if (neighbour.type == PropType::Chr && neighbour.object >= 0) {
            Chr& chr = stage_.chrs_[neighbour.object];
            float radius;
            float height;
            float always20;
            Chrs::WidthHeight(chr, &radius, &height, &always20);

            bool outside = true;
            if (PointInPolygon(neighbour.position, bounds, 4)) {
                outside = false;
                if (chr.actionType == ChrAction::Die && (chr.chrflags & kChrCrushed) == 0) {
                    chr.chrflags |= kChrCrushed;
                    // TODO(https://github.com/liz-baker/frigate/issues/41): CRUSHED_YELL_SFX
                    // and TANK_CRUSH_MAN_SFX.
                }
            }
            if (outside && PointPolygonCollision(neighbour.position, radius, bounds, 4)) {
                outside = false;
            }
            if (!outside && stage_.aiActions_) {
                const Vec3 at = { tank.runtimePosition[0], neighbour.position[1], tank.runtimePosition[2] };
                stage_.aiActions_->ExplosionDamage(chr, at, kRunOverDamage, true);
            }
        } else if (neighbour.type == PropType::Obj) {
            PropCollider footprint;
            if (objects.CollisionBounds(other, &footprint) && footprint.edges > 0 &&
                PolygonsOverlap(footprint.polygon, footprint.edges, bounds, 4)) {
                // The tank runs destroyable objects down.
                if (PlacedObject* object = objects.ForProp(other)) {
                    stage_.damage_.Detonate(other, kCrushDamage, object->runtimePosition, kItemTankShells, 0);
                    penalty = kTankDamagePenaltyTicks;
                }
            }
        }
    }
    return penalty;
}

} // namespace Frigate::Level
