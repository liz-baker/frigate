#include "level/ChrActions.h"

#include <algorithm>
#include <cmath>

#include "level/WeaponTable.h"
#include "model/Random.h"
#include "model/RootMotion.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;

constexpr float DegToRad(int degrees) {
    return static_cast<float>(degrees) * kTau / 360.0f;
}

// CHRLV_TICK_DEAD_CHECK: the ticks a removed character fades over.
constexpr int kDeadFadeTicks = 90;
// CHRLV_RECENT_TIME_CHECK: how long after walking a character may sidestep.
constexpr int kRecentTimeCheck = 181;

// The frame of the sneeze animation the sound cue sits at, and of the alarm
// animation where the alarm goes off.
constexpr float kSneezeCueFrame = 42.0f;
constexpr float kAlarmFrame = 60.0f;

// The clips the actions start (animation_table_ptrs1's names).
constexpr const char* kIdle = "idle";
constexpr const char* kIdleUnarmed = "idle_unarmed";
constexpr const char* kWalking = "walking";
constexpr const char* kWalkingUnarmed = "walking_unarmed";
constexpr const char* kWalkingFemale = "walking_female";
constexpr const char* kRunning = "running";
constexpr const char* kRunningOneHanded = "running_one_handed_weapon";
constexpr const char* kRunningFemale = "running_female";
constexpr const char* kSprinting = "sprinting";
constexpr const char* kSprintingOneHanded = "sprinting_one_handed_weapon";

Frigate::Vec3 ToPlayback(const Vec3& v) {
    return { v[0], v[1], v[2] };
}

} // namespace

// -- Animations ---------------------------------------------------------------

const AnimationClip* ChrActions::Clip(const std::string& name) {
    if (models_ == nullptr || name.empty()) {
        return nullptr;
    }
    auto found = clips_.find(name);
    if (found == clips_.end()) {
        found = clips_.emplace(name, models_->LoadClip(name)).first;
    }
    return found->second;
}

const AnimationClip* ChrActions::ClipForId(int id) {
    return table_ ? Clip(table_->Character(id)) : nullptr;
}

bool ChrActions::Animate(Chr& chr, const std::string& name, bool flip, float startFrame, float speed, float merge) {
    const AnimationClip* clip = Clip(name);
    if (!chr.model || clip == nullptr) {
        return false;
    }
    chr.model->Playback().SetAnimation(clip, flip, startFrame, speed, merge);
    return true;
}

bool ChrActions::AnimateId(Chr& chr, int id, bool flip, float startFrame, float speed, float merge) {
    const AnimationClip* clip = ClipForId(id);
    if (!chr.model || clip == nullptr) {
        return false;
    }
    chr.model->Playback().SetAnimation(clip, flip, startFrame, speed, merge);
    return true;
}

bool ChrActions::IsPlaying(const Chr& chr, const std::string& name) {
    if (!chr.model) {
        return false;
    }
    const AnimationClip* playing = chr.model->Playback().anim;
    return playing != nullptr && playing == Clip(name);
}

float ChrActions::AnimationFrame(const Chr& chr) {
    return chr.model ? chr.model->Playback().frame : 0.0f;
}

float ChrActions::AnimationLength(const Chr& chr) {
    return chr.model ? chr.model->Playback().AnimEndFrame() : 0.0f;
}

bool ChrActions::AnimationEnded(const Chr& chr) {
    return AnimationFrame(chr) >= AnimationLength(chr);
}

float ChrActions::LastFrame(const Chr& chr) {
    // The current clip's frame count less one (unk04 - 1).
    const AnimationClip* clip = chr.model ? chr.model->Playback().anim : nullptr;
    return clip != nullptr ? static_cast<float>(clip->frameCount) - 1.0f : -1.0f;
}

void ChrActions::EndAt(Chr& chr, float frame) {
    if (chr.model) {
        chr.model->Playback().SetAnimEndFrame(frame);
    }
}

void ChrActions::Loop(Chr& chr, float loopFrame, float merge) {
    if (chr.model) {
        chr.model->Playback().SetAnimLooping(loopFrame, merge);
    }
}

bool ChrActions::Mirrored(const Chr& chr) {
    return chr.model && chr.model->Playback().mirror;
}

float ChrActions::ClipStride(Chr& chr, const char* name, bool multiplyFirst) {
    const AnimationClip* clip = Clip(name);
    if (!chr.model || clip == nullptr) {
        return 0.0f;
    }
    const ModelPlayback& playback = chr.model->Playback();
    if (playback.joints == nullptr || clip->frameCount == 0) {
        return 0.0f;
    }
    // sub_GAME_7F000290: the clip's root travel along z, frame by frame.
    int32_t sum = 0;
    for (uint32_t frame = 0; frame + 1 < clip->frameCount; frame++) {
        sum += static_cast<int32_t>(
            SampleRootMotion(*clip, *playback.joints, playback.rootJoint, false, static_cast<int32_t>(frame)).pos[2]);
    }
    const float frames = static_cast<float>(clip->frameCount);
    if (multiplyFirst) {
        return (0.10000001f * static_cast<float>(sum)) / frames;
    }
    return (static_cast<float>(sum) * 0.10000001f) / frames;
}

const ChrActions::Strides& ChrActions::StridesFor(Chr& chr) {
    if (!strides_) {
        // initWeaponAnimGroups' D_80030984 to D_800309A4.
        Strides strides;
        strides.walk = ClipStride(chr, kWalking, false);
        strides.run = ClipStride(chr, kRunning, false);
        strides.sprint = ClipStride(chr, kSprinting, false);
        strides.walkUnarmed = ClipStride(chr, kWalkingUnarmed, false);
        strides.runOneHanded = ClipStride(chr, kRunningOneHanded, false);
        strides.sprintOneHanded = ClipStride(chr, kSprintingOneHanded, false);
        strides.walkFemale = ClipStride(chr, kWalkingFemale, false);
        strides.runFemale = ClipStride(chr, kRunningFemale, true);
        strides.sprintOneHandedAgain = ClipStride(chr, kSprintingOneHanded, false);
        strides_ = strides;
    }
    return *strides_;
}

float ChrActions::AnimationStride(Chr& chr) {
    const Strides& strides = StridesFor(chr);
    float factor = strides.walk;
    if (IsPlaying(chr, kSprinting)) {
        factor = strides.sprint;
    } else if (IsPlaying(chr, kRunning)) {
        factor = strides.run;
    } else if (IsPlaying(chr, kSprintingOneHanded)) {
        factor = strides.sprintOneHanded;
    } else if (IsPlaying(chr, kRunningOneHanded)) {
        factor = strides.runOneHanded;
    } else if (IsPlaying(chr, kWalkingUnarmed)) {
        factor = strides.walkUnarmed;
    } else if (IsPlaying(chr, kRunningFemale)) {
        factor = strides.runFemale;
    } else if (IsPlaying(chr, kWalkingFemale)) {
        factor = strides.walkFemale;
    }
    // The game's second test for the one-handed sprint repeats the first, so
    // its own stride is never used.
    return (chr.model ? chr.model->Playback().scale : 1.0f) * factor * 9.999999f;
}

// -- Held weapons ---------------------------------------------------------------

bool ChrActions::OneHanded(int prop) const {
    if (prop < 0) {
        return false;
    }
    const PlacedObject* weapon = scene_.objects->ForProp(prop);
    return weapon != nullptr && host_.WeaponHasFlag(weapon->weaponNum, WeaponFlag::OnlyOneHanded);
}

bool ChrActions::EitherHandFree(const Chr& chr) const {
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);
    return (left >= 0 && right >= 0) || (left < 0 && right < 0) || OneHanded(left) || OneHanded(right);
}

// -- Starting animations -----------------------------------------------------------

void ChrActions::StartIdleAnimation(Chr& chr, float duration, bool unarmed, bool mirror) {
    if (unarmed) {
        Animate(chr, kIdleUnarmed, mirror, 0.0f, 0.25f, duration);
        Loop(chr, 0.0f, 16.0f);
    } else {
        Animate(chr, kIdle, mirror, 0.0f, 0.25f, duration);
        Loop(chr, 0.0f, 16.0f);
        EndAt(chr, 120.0f);
    }
}

void ChrActions::StandUp(Chr& chr) {
    // chrlvKneelingAnimationRelated.
    if (chr.actionType == ChrAction::Kneel) {
        StopFiring(chr);

        chr.actionType = ChrAction::Stand;
        chr.actStand.prestand = 1;
        chr.actStand.faceEntityType = 0;
        chr.actStand.faceEntityId = 0;
        chr.actStand.reaim = 0;
        chr.actStand.turning = 2;
        chr.actStand.checkFacingWall = 0;
        chr.actStand.wallCount = static_cast<int32_t>(RandomGetNext() % 120u) + 180;
        chr.sleep = 0;

        if (IsPlaying(chr, "fire_kneel_forward_one_handed_weapon_slow")) {
            Animate(chr, "fire_kneel_forward_one_handed_weapon_slow", Mirrored(chr), 109.0f,
                    ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
            EndAt(chr, 140.0f);
        } else {
            Animate(chr, "fire_kneel_left_leg", Mirrored(chr), 120.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
            EndAt(chr, 151.0f);
        }
        return;
    }
    ai_->MergeKneelToStand(chr, 16.0f);
}

void ChrActions::MergeToStandFacingWalls(Chr& chr, float mergeTime) {
    // chrlvIdleAnimationRelated7F023E14.
    ai_->MergeKneelToStand(chr, mergeTime);
    chr.actStand.checkFacingWall = 1;
}

void ChrActions::StandUpFacingWalls(Chr& chr) {
    // chrlvKneelingAnimationRelated7F023E48.
    StandUp(chr);
    chr.actStand.checkFacingWall = 1;
}

void ChrActions::Stop(Chr& chr) {
    StandUpFacingWalls(chr);
}

void ChrActions::Kneel(Chr& chr) {
    // chrKneelChooseAnimation.
    StopFiring(chr);

    if (EitherHandFree(chr)) {
        const bool mirror = (RandomGetNext() & 1) != 0;
        Animate(chr, "fire_kneel_forward_one_handed_weapon_slow", mirror, 0.0f, ai_->SpeedRating(chr, 0.5f, 0.8f),
                16.0f);
        EndAt(chr, 28.0f);
    } else if (Held(chr, HandRight) >= 0 || Held(chr, HandLeft) >= 0) {
        Animate(chr, "fire_kneel_left_leg", Held(chr, HandLeft) >= 0, 0.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
        EndAt(chr, 27.0f);
    }

    chr.actionType = ChrAction::Kneel;
    chr.sleep = 0;
}

void ChrActions::PlayAnimation(Chr& chr, int animation, int startFrame, int endFrame, uint8_t bitfield,
                               int interpolationTicks) {
    // chrlvPerformAnimationForActor.
    constexpr uint8_t kMirror = 0x01;
    constexpr uint8_t kUnknown = 0x02;
    constexpr uint8_t kHoldLastFrame = 0x04;
    constexpr uint8_t kPlaySfx = 0x08;
    constexpr uint8_t kIdleWhenComplete = 0x10;
    constexpr uint8_t kTranslationScale4x = 0x20;
    constexpr uint8_t kNoTranslation = 0x40;
    constexpr uint8_t kReverse = 0x80;

    float speed = 0.5f;
    if ((bitfield & kReverse) != 0) {
        speed = -0.5f;
    }

    StopFiring(chr);
    AnimateId(chr, animation, (bitfield & kMirror) != 0, static_cast<float>(startFrame), speed,
              static_cast<float>(interpolationTicks));

    if (endFrame >= 0) {
        EndAt(chr, static_cast<float>(endFrame));
    }
    if ((bitfield & kTranslationScale4x) != 0 && chr.model) {
        chr.model->Playback().animTranslationScale *= 4.0f;
    }

    chr.chrflags &= ~ChrFlag::SoundCuePlayed;
    chr.actionType = ChrAction::Anim;

    chr.actAnim.unnamed2C = (bitfield & kUnknown) != 0;
    chr.actAnim.holdLastFrame = (bitfield & kHoldLastFrame) != 0;
    chr.actAnim.playSfx = (bitfield & kPlaySfx) != 0;
    chr.actAnim.idleOnEnd = (bitfield & kIdleWhenComplete) != 0;
    chr.actAnim.noTranslate = (bitfield & kNoTranslation) != 0;

    chr.sleep = chr.actAnim.idleOnEnd ? static_cast<int8_t>(interpolationTicks) : 0;
}

void ChrActions::StartAlarm(Chr& chr) {
    // chrStartAlarmChooseAnimation.
    const bool left = Held(chr, HandLeft) >= 0;
    const bool right = Held(chr, HandRight) >= 0;
    bool flip = false;

    if (left && !right) {
        flip = true;
    } else if ((left && right) || (!left && !right)) {
        flip = (RandomGetNext() & 1) != 0;
    }

    StopFiring(chr);

    chr.actionType = ChrAction::StartAlarm;
    chr.sleep = 0;

    Animate(chr, "extending_left_hand", flip, 40.0f, 1.0f, 16.0f);
    EndAt(chr, 82.0f);
}

void ChrActions::StartSpotBond(Chr& chr, float interpolation) {
    // chrlvSpotBondAnimationRelated.
    const bool left = Held(chr, HandLeft) >= 0;
    const bool right = Held(chr, HandRight) >= 0;
    bool flip = false;
    if (left && !right) {
        flip = true;
    } else if ((left && right) || (!left && !right)) {
        flip = (RandomGetNext() & 1) != 0;
    }

    const float speed = ai_->SpeedRating(chr, 0.6f, 0.96000004f);
    Animate(chr, "spotting_bond", flip, 10.0f, speed, interpolation);
    EndAt(chr, 52.0f);
}

void ChrActions::PointAtBond(Chr& chr) {
    // chrlvActorShuffleFeet.
    const float angle = ai_->AngleToBond(chr);

    if (angle < 0.17453294f || angle > 6.1086526f) {
        StartSpotBond(chr, 16.0f);
        StopFiring(chr);
        chr.actionType = ChrAction::Surprised;
        chr.sleep = 0;
        return;
    }

    if (!ai_->HasStoppedOrPatrolling(chr)) {
        StandUp(chr);
    }
}

void ChrActions::RaiseArms(Chr& chr) {
    // chrlvSurrenderAnimationRelated.
    StopFiring(chr);
    chr.actionType = ChrAction::Surprised;
    chr.sleep = 0;
    const bool mirror = (RandomGetNext() & 1) != 0;
    Animate(chr, "surrendering_armed", mirror, 0.0f, ai_->SpeedRating(chr, 0.35f, 0.56f), 16.0f);
    EndAt(chr, 7.0f);
}

void ChrActions::LookSurprised(Chr& chr) {
    // chrlvActorLookFlustered.
    const uint32_t variant = RandomGetNext() % 3u;

    StopFiring(chr);

    chr.actionType = ChrAction::Surprised;
    chr.sleep = 0;
    const bool mirror = (RandomGetNext() & 1) != 0;
    Animate(chr, "look_around", mirror, 17.0f, 0.6f, 16.0f);

    if (variant == 0) {
        EndAt(chr, ai_->SpeedRating(chr, 38.0f, 8.0f));
    } else if (variant == 1) {
        EndAt(chr, ai_->SpeedRating(chr, 66.0f, 8.0f));
    } else {
        EndAt(chr, ai_->SpeedRating(chr, 96.0f, 8.0f));
    }
}

void ChrActions::FadeOut(Chr& chr) {
    // chrlvActorFadeAway.
    if (chr.actionType != ChrAction::Dead) {
        StopFiring(chr);
        chr.actionType = ChrAction::Dead;
        chr.actDead.fadeTicks = -1;
        chr.sleep = 0;
    }
}

bool ChrActions::FaceTarget(Chr& chr, int type, int id) {
    // check_set_actor_standing_still.
    if (!ai_->IsNotDeadOrShot(chr)) {
        return false;
    }
    if (chr.actionType != ChrAction::Stand) {
        StandUp(chr);
    }
    chr.actStand.faceEntityType = type;
    chr.actStand.faceEntityId = id;
    chr.actStand.reaim = 0;
    chr.actStand.checkFacingWall = 0;
    return true;
}

// -- Facing ----------------------------------------------------------------------

float ChrActions::AngleTo(const Chr& chr, const Vec3& position) const {
    // get_distance_actor_to_position.
    const Vec3& mine = (*scene_.props)[chr.prop].position;
    const float heading = ChrHeading(chr);
    const float between = GameAtan2f(position[0] - mine[0], position[2] - mine[2]);
    float angle = between - heading;
    if (between < heading) {
        angle += kTau;
    }
    return angle;
}

Vec3 ChrActions::TargetLocation(Chr& chr, int flags, int id, int* tile) {
    // chrlvGetChrOrPresetLocation.
    if ((flags & 4) != 0) {
        const Chr* guard = scene_.chrs->FindById(chr, id);
        if (guard == nullptr || !guard->inUse || guard->prop < 0) {
            guard = &chr;
        }
        *tile = (*scene_.props)[chr.prop].stan;
        return (*scene_.props)[guard->prop].position;
    }

    if ((flags & 8) != 0) {
        if (const Pad* pad = ai_->PadForId(Chrs::ResolvePadId(chr, id))) {
            *tile = pad->stanTile;
            return pad->position;
        }
    }

    const AiPlayer player = host_.Player();
    if (player.prop >= 0) {
        *tile = (*scene_.props)[player.prop].stan;
        return (*scene_.props)[player.prop].position;
    }
    *tile = kNoTile;
    return {};
}

float ChrActions::AngleToFaceTarget(Chr& chr, int type, int id) {
    // chrlvDistanceToChrRelated.
    if ((type & 2) != 0) {
        return 0.0f;
    }
    if ((type & 0x10) != 0) {
        float angle = (static_cast<float>(id) * kTau) / 65536.0f;
        angle -= ChrHeading(chr);
        if (angle < 0.0f) {
            angle += kTau;
        }
        return angle;
    }
    int tile = kNoTile;
    const Vec3 target = TargetLocation(chr, type, id, &tile);
    return AngleTo(chr, target);
}

int ChrActions::TurnToFace(Chr& chr, int state, float lastFrame, float rate, float offset) {
    // chrlvSetSubroty.
    if (state == 2) {
        return state;
    }
    ModelPlayback& playback = chr.model->Playback();
    const float frame = playback.frame;
    float heading = ChrHeading(chr);
    const float step = 0.06283186f * rate * scene_.chrs->Timing().globalTimerDelta * playback.playSpeed;

    float distance;
    if (chr.actionType == ChrAction::Attack) {
        distance = AngleToFaceTarget(chr, chr.actAttack.attackType, chr.actAttack.entityId);
    } else if (chr.actionType == ChrAction::Stand) {
        distance = AngleToFaceTarget(chr, chr.actStand.faceEntityType, chr.actStand.faceEntityId);
    } else {
        const AiPlayer player = host_.Player();
        distance = AngleTo(chr, (*scene_.props)[player.prop].position);
    }

    distance = distance - offset;
    if (distance < 0.0f) {
        distance += kTau;
    }

    if (distance < step || (kTau - step) < distance) {
        heading += distance;
        if (heading >= kTau) {
            heading -= kTau;
        }
        playback.SetHeading(heading);
        state = 3;
    } else if (distance < kPi) {
        heading += step;
        if (heading >= kTau) {
            heading -= kTau;
        }
        playback.SetHeading(heading);
    } else {
        heading -= step;
        if (heading < 0.0f) {
            heading += kTau;
        }
        playback.SetHeading(heading);
    }

    if (lastFrame <= frame) {
        state = 2;
    }
    return state;
}

// -- The actions' ticks ------------------------------------------------------------

void ChrActions::Tick(Chr& chr) {
    // The decomp's characters always have a model; a source with none has
    // nothing to animate.
    if (!chr.model || chr.prop < 0) {
        return;
    }
    switch (chr.actionType) {
        case ChrAction::Stand:
            TickStand(chr);
            break;
        case ChrAction::Kneel:
            chr.sleep = 0;
            break;
        case ChrAction::Anim:
            TickAnim(chr);
            break;
        case ChrAction::Sidestep:
            if (AnimationEnded(chr)) {
                ai_->SetTargetToPlayer(chr);
                MergeToStandFacingWalls(chr, 10.0f);
            }
            break;
        case ChrAction::JumpOut:
            if (AnimationEnded(chr)) {
                ai_->SetTargetToPlayer(chr);
                StandUpFacingWalls(chr);
            }
            break;
        case ChrAction::Dead:
            TickDead(chr);
            break;
        case ChrAction::RunPos:
            TickRunPos(chr);
            break;
        case ChrAction::Patrol:
            TickPatrol(chr);
            break;
        case ChrAction::GoPos:
            TickGoPos(chr);
            break;
        case ChrAction::Test:
            if (AnimationEnded(chr)) {
                StandUp(chr);
            }
            break;
        case ChrAction::Surprised:
            TickSurprised(chr);
            break;
        case ChrAction::StartAlarm:
            if (AnimationFrame(chr) >= kAlarmFrame) {
                host_.AlarmActivate();
            }
            if (AnimationEnded(chr)) {
                StandUpFacingWalls(chr);
            }
            break;
        case ChrAction::Die:
            TickDie(chr);
            break;
        case ChrAction::Argh:
            TickArgh(chr);
            break;
        case ChrAction::PreArgh:
            TickPreArgh(chr);
            break;
        case ChrAction::Attack:
            TickAttack(chr);
            break;
        case ChrAction::AttackWalk:
            TickAttackWalk(chr);
            break;
        case ChrAction::AttackRoll:
            TickAttackRoll(chr);
            break;
        case ChrAction::Surrender:
            TickSurrender(chr);
            break;
        case ChrAction::ThrowGrenade:
            TickThrowGrenade(chr);
            break;
        case ChrAction::BondIntro:
            TickBondIntro(chr);
            break;
        default:
            break;
    }
}

void ChrActions::TickSurprised(Chr& chr) {
    if (!AnimationEnded(chr)) {
        return;
    }
    if (IsPlaying(chr, "surrendering_armed") || IsPlaying(chr, "spotting_bond")) {
        MergeToStandFacingWalls(chr, 26.0f);
    } else {
        StandUpFacingWalls(chr);
    }
}

void ChrActions::TickAnim(Chr& chr) {
    if (!chr.actAnim.holdLastFrame && AnimationEnded(chr)) {
        StandUp(chr);
    }

    if (IsPlaying(chr, "sneeze") && AnimationFrame(chr) >= kSneezeCueFrame &&
        (chr.chrflags & ChrFlag::SoundCuePlayed) == 0) {
        // TODO(https://github.com/liz-baker/frigate/issues/41): the sneeze, when
        // the player is within 800 units on an even frame.
        chr.chrflags |= ChrFlag::SoundCuePlayed;
    }

    if (chr.sleep <= 0 && chr.actAnim.idleOnEnd) {
        chr.sleep = static_cast<int8_t>((RandomGetNext() % 5u) + 0xE);
    }
}

void ChrActions::TickDead(Chr& chr) {
    if (chr.actDead.fadeTicks >= 0) {
        chr.actDead.fadeTicks += scene_.chrs->Timing().clockTimer;
        if (chr.actDead.fadeTicks >= kDeadFadeTicks) {
            chr.hidden |= ChrHidden::Remove;
        } else {
            chr.fadeAlpha = static_cast<uint8_t>(((kDeadFadeTicks - chr.actDead.fadeTicks) * 0xFF) / kDeadFadeTicks);
        }
        return;
    }
    chr.actDead.fadeTicks = 0;
}

void ChrActions::TickStand(Chr& chr) {
    if (chr.sleep > 0) {
        return;
    }

    if (chr.actStand.prestand != 0) {
        if (AnimationEnded(chr)) {
            ai_->StartIdle(chr, 8.0f);
            chr.actStand.prestand = 0;
        }
        chr.sleep = 0;
        return;
    }

    if (chr.actStand.faceEntityType > 0) {
        if (chr.actStand.reaim) {
            chr.actStand.turning = TurnToFace(chr, chr.actStand.turning, LastFrame(chr), 1.0f, 0.0f);

            if (chr.actStand.turning != 1) {
                ai_->StartIdle(chr, 8.0f);
                chr.actStand.reaim = 0;

                if ((chr.actStand.faceEntityType & 0x10) != 0) {
                    chr.actStand.faceEntityType = 0;
                }
            }
        } else {
            const float angle = AngleToFaceTarget(chr, chr.actStand.faceEntityType, chr.actStand.faceEntityId);
            if (angle > 0.34906587f && angle < 5.9341197f) {
                chr.actStand.reaim = 1;
                chr.actStand.turning = 1;

                if (EitherHandFree(chr)) {
                    const bool mirror = (RandomGetNext() & 1) != 0;
                    Animate(chr, kWalkingUnarmed, mirror, 0.0f, 0.5f, 16.0f);
                    EndAt(chr, LastFrame(chr));
                } else if (Held(chr, HandRight) >= 0 || Held(chr, HandLeft) >= 0) {
                    Animate(chr, kWalking, Held(chr, HandLeft) >= 0, 0.0f, 0.5f, 16.0f);
                    EndAt(chr, LastFrame(chr));
                }
            } else if ((chr.actStand.faceEntityType & 0x10) != 0) {
                chr.actStand.faceEntityType = 0;
            }
        }

        chr.sleep = 0;
        return;
    }

    chr.sleep = static_cast<int8_t>((RandomGetNext() % 5u) + 0xE);

    if (chr.actStand.checkFacingWall == 0) {
        return;
    }
    if ((chr.chrflags & ChrFlag::Unnamed0x80) != 0) {
        chr.actStand.checkFacingWall = 0;
        return;
    }

    chr.actStand.wallCount -= chr.sleep;
    if (chr.actStand.wallCount >= 0) {
        return;
    }

    // How far the character could walk in each of eight directions round it.
    std::array<float, 8> clear{};
    const float heading = ChrHeading(chr);
    float angle = heading;
    for (float& distance : clear) {
        angle += DegToRad(45);
        if (angle >= kTau) {
            angle -= kTau;
        }
        distance = ClearDistance(chr, angle, 1000.0f, 0, 0.0f, 1.0f);
    }

    // The directions, nearest wall first (a selection sort).
    std::array<int, 8> order{};
    for (int i = 0; i < 8; i++) {
        order[static_cast<size_t>(i)] = i;
    }
    for (int i = 0; i < 7; i++) {
        int smallest = i;
        for (int j = i + 1; j < 8; j++) {
            if (clear[static_cast<size_t>(order[static_cast<size_t>(j)])] <
                clear[static_cast<size_t>(order[static_cast<size_t>(smallest)])]) {
                smallest = j;
            }
        }
        std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(smallest)]);
    }

    int index = -1;
    if (clear[0] < 490.0f) {
        if (clear[static_cast<size_t>(order[4])] < 200.0f) {
            index = 7;
        } else if (order[0] == 0 || order[1] == 0 || order[2] == 0) {
            if ((order[3] == 4 || order[4] == 4) && (RandomGetNext() % 3u) == 0) {
                index = order[3] == 4 ? 3 : 4;
            } else {
                index = 5 + static_cast<int>(RandomGetNext() % 3u);
            }
        } else if ((order[0] == 1 || order[0] == 7) && order[5] != 0 && order[6] != 0 && order[7] != 0) {
            index = 5 + static_cast<int>(RandomGetNext() % 3u);
        }
    }

    if (index >= 0) {
        const int direction = order[static_cast<size_t>(index)];
        angle = (static_cast<float>(direction) * kTau * 0.125f) + heading;
        if (angle >= kTau) {
            angle -= kTau;
        }
        FaceTarget(chr, 0x10, static_cast<int>((angle * 65536.0f) / kTau));
    } else {
        chr.actStand.checkFacingWall = 0;
    }
}

// -- Sidestepping -------------------------------------------------------------------

namespace {

// The ids of the sides (GUNRIGHT is 0).
constexpr int kSideRight = 0;

} // namespace

bool ChrActions::SidestepClear(Chr& chr, const Vec3& to, const Vec3& direction) {
    // chrlvCall7F0B0E24WithChrWidthHeight.
    constexpr int kTested = CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker;
    const Prop& prop = (*scene_.props)[chr.prop];
    Stan& stan = *scene_.stan;

    const float chrX = direction[0] * chr.chrWidth * 1.2f;
    const float chrZ = direction[2] * chr.chrWidth * 1.2f;
    bool clear = false;

    float width;
    float height;
    float always20;
    Chrs::WidthHeight(chr, &width, &height, &always20);
    ChrAi::SetMoving(chr, false);

    float midX = prop.position[0] + chrZ;
    float midZ = prop.position[2] - chrX;
    float endX = to[0] + chrZ + chrX;
    float endZ = (to[2] - chrX) + chrZ;

    int tile = prop.stan;
    if (stan.TestLineUnobstructed(&tile, prop.position[0], prop.position[2], midX, midZ, kTested, height, always20,
                                  0.0f, 1.0f) != 0 &&
        stan.TestLineUnobstructed(&tile, midX, midZ, endX, endZ, kTested, height, always20, 0.0f, 1.0f) != 0) {
        midX = prop.position[0] - chrZ;
        midZ = prop.position[2] + chrX;
        endX = (to[0] - chrZ) + chrX;
        endZ = to[2] + chrX + chrZ;

        tile = prop.stan;
        if (stan.TestLineUnobstructed(&tile, prop.position[0], prop.position[2], midX, midZ, kTested, height, always20,
                                      0.0f, 1.0f) != 0 &&
            stan.TestLineUnobstructed(&tile, midX, midZ, endX, endZ, kTested, height, always20, 0.0f, 1.0f) != 0) {
            clear = true;
        }
    }

    ChrAi::SetMoving(chr, true);
    return clear;
}

bool ChrActions::SideIsClear(Chr& chr, int side, float distance, bool turnedAway) {
    // sub_GAME_7F02A1E8 (a side of the character) and sub_GAME_7F02A0EC (a side
    // of the line to the player).
    const Prop& prop = (*scene_.props)[chr.prop];
    Vec3 direction{};
    if (turnedAway) {
        // chrlvNormDistanceToPlayer.
        const AiPlayer player = host_.Player();
        const Vec3& bond = (*scene_.props)[player.prop].position;
        float dx = bond[0] - prop.position[0];
        float dz = bond[2] - prop.position[2];
        const float norm = std::sqrt((dx * dx) + (dz * dz));
        dx = dx / norm;
        dz = dz / norm;
        if (side != kSideRight) {
            direction = { dz, 0.0f, -dx };
        } else {
            direction = { -dz, 0.0f, dx };
        }
    } else {
        // chrlvModelRotyRelated.
        const float heading = ChrHeading(chr);
        if (side != 0) {
            direction = { GameCosf(heading), 0.0f, -GameSinf(heading) };
        } else {
            direction = { -GameCosf(heading), 0.0f, GameSinf(heading) };
        }
    }

    const Vec3 to = { prop.position[0] + (direction[0] * distance), prop.position[1],
                      prop.position[2] + (direction[2] * distance) };
    return SidestepClear(chr, to, direction);
}

bool ChrActions::Sidestep(Chr& chr) {
    // actor_steps_sideways.
    if (!ai_->IsNotDeadOrShot(chr)) {
        return false;
    }
    const float change = ai_->AngleToBond(chr);
    if (change < DegToRad(45) || change > DegToRad(360 - 45) ||
        (change > DegToRad(180 - 45) && change < DegToRad(180 + 45))) {
        const int side = (RandomGetNext() & 1) == 0;
        if (SideIsClear(chr, side, 100.0f, false)) {
            StartSidestep(chr, side);
            return true;
        }
        const int other = side == 0;
        if (SideIsClear(chr, other, 100.0f, false)) {
            StartSidestep(chr, other);
            return true;
        }
    }
    return false;
}

bool ChrActions::SideHop(Chr& chr) {
    // actor_hops_sideways.
    if (!ai_->IsNotDeadOrShot(chr)) {
        return false;
    }
    const float change = ai_->AngleToBond(chr);
    if (change < DegToRad(45) || change > DegToRad(360 - 45) ||
        (change > DegToRad(180 - 45) && change < DegToRad(180 + 45))) {
        const int side = (RandomGetNext() & 1) == 0;
        if (SideIsClear(chr, side, 200.0f, false)) {
            StartSideHop(chr, side);
            return true;
        }
        const int other = side == 0;
        if (SideIsClear(chr, other, 200.0f, false)) {
            StartSideHop(chr, other);
            return true;
        }
    }
    return false;
}

bool ChrActions::SideRun(Chr& chr) {
    // actor_jogs_sideways.
    if (!ai_->IsNotDeadOrShot(chr) || (scene_.chrs->Timing().globalTimer - chr.lastWalk60) < kRecentTimeCheck) {
        return false;
    }
    const Prop& prop = (*scene_.props)[chr.prop];
    const float distance = (static_cast<float>(RandomGetNext()) * (1.0f / 4294967295.0f) * 200.0f) + 200.0f;

    const auto direction = [&](bool left) {
        const AiPlayer player = host_.Player();
        const Vec3& bond = (*scene_.props)[player.prop].position;
        float dx = bond[0] - prop.position[0];
        float dz = bond[2] - prop.position[2];
        const float norm = std::sqrt((dx * dx) + (dz * dz));
        dx = dx / norm;
        dz = dz / norm;
        return left ? Vec3{ dz, 0.0f, -dx } : Vec3{ -dz, 0.0f, dx };
    };

    Vec3 vector = direction((RandomGetNext() & 1) == 0 ? true : false);
    Vec3 target = { (vector[0] * distance) + prop.position[0], prop.position[1],
                    (vector[2] * distance) + prop.position[2] };
    if (SidestepClear(chr, target, vector)) {
        StartRunToPosition(chr, target);
        return true;
    }

    vector = { -vector[0], vector[1], -vector[2] };
    target = { (vector[0] * distance) + prop.position[0], prop.position[1], (vector[2] * distance) + prop.position[2] };
    if (SidestepClear(chr, target, vector)) {
        StartRunToPosition(chr, target);
        return true;
    }
    return false;
}

void ChrActions::StartSidestep(Chr& chr, int side) {
    // chrlvSideStepAnimationRelated.
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);
    int hand = 0;
    int slide = 0;

    if (left >= 0 && right >= 0) {
        hand = static_cast<int>(RandomGetNext() & 1);
        slide = static_cast<int>(RandomGetNext() & 1);
    } else if (!OneHanded(left)) {
        if (!OneHanded(right) && (left >= 0 || right >= 0)) {
            hand = left >= 0;
            slide = static_cast<int>(RandomGetNext() & 1);
        }
    }

    StopFiring(chr);

    chr.actionType = ChrAction::Sidestep;
    chr.sleep = 0;

    if (slide == 0) {
        Animate(chr, "side_step_left", side == kSideRight, 5.0f, ai_->SpeedRating(chr, 0.55f, 0.88000005f), 16.0f);
        EndAt(chr, 27.0f);
        return;
    }

    if ((side != kSideRight && hand == 0) || (side == kSideRight && hand != 0)) {
        Animate(chr, "slide_left", hand != 0, 5.0f, ai_->SpeedRating(chr, 0.7f, 1.12f), 16.0f);
        EndAt(chr, 34.0f);
    } else {
        Animate(chr, "slide_right", hand != 0, 5.0f, ai_->SpeedRating(chr, 0.7f, 1.12f), 16.0f);
        EndAt(chr, 32.0f);
    }
}

void ChrActions::StartSideHop(Chr& chr, int side) {
    // chrlvFireJumpToSideAnimationRelated.
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);
    int hopSide = kSideRight;

    if (left >= 0 && right < 0) {
        hopSide = 1;
    } else if ((left >= 0 && right >= 0) || (left < 0 && right < 0) || OneHanded(left) || OneHanded(right)) {
        hopSide = static_cast<int>(RandomGetNext() & 1);
    }

    StopFiring(chr);

    chr.actionType = ChrAction::JumpOut;
    chr.sleep = 0;

    if ((side != kSideRight && hopSide == kSideRight) || (side == kSideRight && hopSide != kSideRight)) {
        if ((RandomGetNext() & 1) != 0) {
            Animate(chr, "fire_jump_to_side_left", hopSide != 0, 5.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
            EndAt(chr, 49.0f);
        } else {
            Animate(chr, "fire_jump_to_side_right", hopSide != 0, 130.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
            EndAt(chr, 173.0f);
        }
        return;
    }

    if ((RandomGetNext() & 1) != 0) {
        Animate(chr, "fire_jump_to_side_right", hopSide != 0, 20.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
        EndAt(chr, 63.0f);
    } else {
        Animate(chr, "fire_jump_to_side_left", hopSide != 0, 91.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
        EndAt(chr, 136.0f);
    }
}

} // namespace Frigate::Level
