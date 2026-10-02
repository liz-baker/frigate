#pragma once

#include <array>
#include <cstdint>

#include "level/Beams.h"
#include "level/Matrix.h"
#include "level/ModelHandle.h"

namespace Frigate::Level {

// What an autogun keeps past the ObjectRecord (AutogunRecord): where it
// points, how fast it is turning, and the barrel spin and bullet beam of its
// fire.
struct AutogunState {
    // rot_related and unk98: the heading and pitch it rests at; the pad it was
    // given points it there, and an idle gun with the double flag wanders
    // between random ones.
    float restYaw = 0.0f;
    float restPitch = 0.0f;
    // unk90 and unk94, unk9C and unkA0: the heading and pitch it points at, and
    // their speeds.
    float yaw = 0.0f;
    float yawSpeed = 0.0f;
    float pitch = 0.0f;
    float pitchSpeed = 0.0f;
    // unk88 and unk8C: the most it can turn either way from its rest heading.
    float limitMax = 0.0f;
    float limitMin = 0.0f;
    // speed: how fast it turns once it has seen the player, and aimdist: how
    // far it sees.
    float speed = 0.0f;
    float aimDistance = 0.0f;
    // unkAC: shots made, which the muzzle flashes and beams alternate on.
    int32_t shots = 0;
    // unkB0 and unkB4: the speed the barrels spin at and how far round they are.
    float spinSpeed = 0.0f;
    float spinAngle = 0.0f;
    // unkB8 and unkBC: the tick it last had the player in its sights, and the
    // tick it first did (-1 for never).
    int32_t lastSighted = -1;
    int32_t firstSighted = -1;
    // unkC0: the tick the sound of its fire was last started (the sound itself is
    // not played).
    int32_t soundTick = -1;
    // is_active: it is tracking the player and firing.
    bool active = false;
    // unkD4: the damage it has built up on the player towards the next hit.
    float damageBuilt = 0.0f;
    // The bullet tracer (beam).
    BeamRecord beam;
    // Whether the muzzle flash nodes in switch slots 5 and 7 show (their
    // Gunfire.visible).
    std::array<bool, 2> flash{};
};

// setupAutogun: the gun for an object placed at `position`, aimed at the pad
// at `padPosition` when it has one. `fixed` holds the file's limits (unk88,
// unk8C), speed and range (see PropDef::autogunFixed).
AutogunState MakeAutogun(const Vec3& position, const Vec3* padPosition, const std::array<int32_t, 4>& fixed);

// The slots of the model that the pose and the muzzle read.
constexpr size_t kAutogunSlotCount = 8;

// How the model's matrices are posed under `base` (the matrix its body is
// drawn with): slots 1 and 2 are the heading and pitch, 3 and 6 the spinning
// barrels and 4 the fixed part, as propobj.c's PROPDEF_AUTOGUN pose builds
// them.
struct AutogunPose {
    // The matrix each slot's node is posed with; meaningful where `posed` is set.
    std::array<Mtxf, kAutogunSlotCount> matrix{};
    std::array<bool, kAutogunSlotCount> posed{};
};

// `base` is the object's matrix with its position (the matrix its body is
// drawn with), `scale` the model's scale.
AutogunPose PoseAutogun(const SwitchGeometry& geometry, const AutogunState& state, const Mtxf& base, float scale);

// modelFindNodeMtx for the muzzle node in switch slot `slot` (5 or 7): the
// pose of the nearest node above it that owns a matrix; the body's matrix
// when nothing does.
Mtxf AutogunNodeMatrix(const SwitchGeometry& geometry, const AutogunPose& pose, const Mtxf& base, size_t slot);

} // namespace Frigate::Level
