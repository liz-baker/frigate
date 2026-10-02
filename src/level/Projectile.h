#pragma once

#include <array>
#include <cstdint>

#include "level/Matrix.h"

namespace Frigate::Level {

// DROPTYPE: how an item a character let go of falls.
namespace DropType {
constexpr int Default = 1;
constexpr int Surrender = 2;
constexpr int ThrowGrenade = 3;
constexpr int Hat = 4;
} // namespace DropType

// PROJECTILEFLAG_*.
namespace ProjectileFlag {
constexpr uint32_t Airborne = 0x00000001;
// A thrown grenade: it bounces and only waits the fuse out.
constexpr uint32_t Bouncy = 0x00000002;
// Moves through the rooms along its path (handles_projectile_motion).
constexpr uint32_t Sticky = 0x00000004;
// The tile it stands on was lost: it falls through the floor.
constexpr uint32_t OffTiles = 0x00000008;
constexpr uint32_t Powered = 0x00000010;
// A rocket still rising to the apex of its launch.
constexpr uint32_t Rising = 0x00000020;
// The thrown grenade's: it falls through doors.
constexpr uint32_t Thrown = 0x00000040;
constexpr uint32_t Launching = 0x00000080;
// The launch was blocked: the first move runs from the player's position.
constexpr uint32_t Blocked = 0x00000100;
constexpr uint32_t NoSpin = 0x00000200;
constexpr uint32_t Falling = 0x00000400;
constexpr uint32_t Sliding = 0x00000800;
constexpr uint32_t Lightweight = 0x40000000;
constexpr uint32_t Free = 0x80000000;
} // namespace ProjectileFlag

// Projectile: the flight record an item that was thrown, dropped or fired
// carries (and that a fallen door or object borrows).
struct Projectile {
    uint32_t flags = ProjectileFlag::Free;
    Vec3 speed{};
    // unk10: the acceleration, and unk1C the extra lift a rocket gains.
    Vec3 acceleration{};
    float lift = 0.0f;
    // mtx: the spin it turns by each tick.
    Mtxf spin = IdentityMtxf();
    // unk60 and unk64: how far through settling to rest the object is and how
    // fast it goes, between the rotations unk68 and unk78 (quaternions).
    float settleProgress = 1.0f;
    float settleRate = 0.0f;
    std::array<float, 4> settleFrom{};
    std::array<float, 4> settleTo{};
    // ownerprop: the prop it left, -1 for none.
    int ownerProp = -1;
    // unk8C: how much speed a bounce keeps; unk90: the bounces so far.
    float bounce = 0.05f;
    uint32_t bounces = 0;
    // unk94: the friction a slide loses.
    float friction = 0.0f;
    // lastSfxTimer and soundSlot.
    int32_t lastSfxTimer = -1;
    uint32_t soundSlot = 0;
    // unkA8: ticks in the air; unkAC: the frame it last hit something.
    uint32_t airTicks = 0;
    int32_t lastImpact = -1;
    // unkB0 and unkB4: the height a rocket climbs to and how fast.
    float apexHeight = 0.0f;
    float apexSpeed = 0.0f;
    int dropType = DropType::Default;
    // refreshrate.
    int32_t refreshRate = 0;
    // unkC0 to unkC8: the scale on the object's three axes.
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    // unkCC: the rooms it travels through, ended by 0xFF.
    std::array<uint8_t, 8> rooms{};
    // unkd4: where a blocked launch was aimed.
    Vec3 launchTarget{};
    // age.
    uint32_t age = 0;
    // obj: the prop of the object it belongs to, -1 for none.
    int prop = -1;
    // unkE8: the frame it was made.
    int32_t created = 0;
};

constexpr int kMaxProjectiles = 20;
constexpr int kMaxEmbedments = 40;

} // namespace Frigate::Level
