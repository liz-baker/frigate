#include <algorithm>
#include <cmath>

#include "level/ChrActions.h"
#include "level/Matrix.h"
#include "level/WeaponTable.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
// The stances of firing_animation_groups.
constexpr int kStanceStill = 0;
constexpr int kStanceWalk = 1;
constexpr int kStanceRun = 2;
constexpr int kStanceSideRight = 3;
constexpr int kStanceSideLeft = 4;
constexpr int kStanceCrouch = 5;
// The groups: a gun held in one hand, in both, no gun, two guns.
constexpr int kGroupOneHanded = 0;
constexpr int kGroupTwoHanded = 1;
constexpr int kGroupUnarmed = 2;
constexpr int kGroupDual = 3;
// How far a sidestep leans the body, and how fast it comes back.
constexpr float kLeanStep = 15.0f;
constexpr float kLeanLimit = 90.0f;
// How long the aim of the character's arms takes to settle.
constexpr int8_t kAimEndTicks = 10;

} // namespace

bool ChrActions::GunMuzzle(Chr& chr, int hand, const Mtxf& viewToWorld, Vec3* out) {
    return MuzzleMatrixPosition(chr, hand, viewToWorld, out);
}

void ChrActions::PosePlayerBody(Chr& chr, PlayerBody& body, bool full) {
    // The part of playerTick that sets up the character of a player the
    // current player sees, ahead of the chrTick that poses it.
    Prop& prop = PropOf(chr);
    ModelPlayback& playback = chr.model->Playback();

    const AnimationClip* anim = nullptr;
    const FiringAnim* firingTable = nullptr;

    if (full) {
        float aimPitch = body.aimPitch;
        float aimYaw = body.aimYaw;
        float animSpeed = 0.0f;
        float endFrame = -1.0f;
        float loopFrame = -1.0f;
        const AnimationClip* current = body.clip;

        if (body.dead) {
            // The body plays the death animation it is in already, or a new one.
            bool found = false;
            if (body.deathClips != nullptr) {
                for (const AnimationClip* clip : *body.deathClips) {
                    if (clip == body.clip) {
                        found = true;
                    }
                }
            }
            if (found) {
                anim = body.clip;
            } else if (body.deathClips != nullptr && !body.deathClips->empty()) {
                anim = (*body.deathClips)[RandomGetNext() % body.deathClips->size()];
            }
            animSpeed = 0.5f;
            aimPitch = 0.0f;
            aimYaw = 0.0f;
        } else {
            const int leftItem = HeldItem(chr, HandLeft);
            const int rightItem = HeldItem(chr, HandRight);
            int group;
            if (leftItem >= 0 && rightItem >= 0) {
                group = kGroupDual;
            } else if (leftItem < 0 && rightItem < 0) {
                group = kGroupUnarmed;
            } else if (leftItem >= 0 && !host_.WeaponHasFlag(leftItem, WeaponFlag::HoldAsGun)) {
                group = kGroupUnarmed;
            } else if (rightItem >= 0 && !host_.WeaponHasFlag(rightItem, WeaponFlag::HoldAsGun)) {
                group = kGroupUnarmed;
            } else if (leftItem >= 0 && host_.WeaponHasFlag(leftItem, WeaponFlag::OnlyOneHanded)) {
                group = kGroupOneHanded;
            } else if (rightItem >= 0 && host_.WeaponHasFlag(rightItem, WeaponFlag::OnlyOneHanded)) {
                group = kGroupOneHanded;
            } else {
                group = kGroupTwoHanded;
            }

            const auto hasTable = [&](int stance) {
                const MultiFiring* multi = combat_ ? combat_->Multi(group, stance) : nullptr;
                return multi != nullptr && multi->entry != nullptr;
            };

            float angle;
            int stance;
            bool recentre = false;
            if (body.crouch == 1 || body.crouch == 0) {
                angle = 1.0f;
                stance = kStanceCrouch;
            } else if (body.speedSideways < 0.0f && hasTable(kStanceSideLeft)) {
                stance = kStanceSideLeft;
                angle = -body.speedSideways;
                if (body.lean < kLeanLimit) {
                    body.lean += kLeanStep;
                }
            } else if (body.speedSideways > 0.0f && hasTable(kStanceSideRight)) {
                stance = kStanceSideRight;
                angle = body.speedSideways;
                if (body.lean > -kLeanLimit) {
                    body.lean -= kLeanStep;
                }
            } else {
                recentre = true;
                float frame = body.speedTheta;
                if (frame < 0.0f) {
                    frame = -frame;
                }
                const float forward = body.speedForwards;
                if (forward < -0.050000001f) {
                    frame = -frame;
                    if (forward < frame) {
                        frame = forward;
                    }
                    if (body.headAnimation == 0 || -0.40000001f < forward) {
                        angle = frame + frame;
                        stance = kStanceWalk;
                        if (angle < -1.0f) {
                            angle = -1.0f;
                        }
                    } else {
                        angle = frame;
                        stance = kStanceRun;
                    }
                } else if (0.050000001f < forward || 0.050000001f < frame) {
                    if (frame < forward) {
                        frame = forward;
                    }
                    if (body.headAnimation == 0 || forward < 0.40000001f) {
                        angle = frame + frame;
                        stance = kStanceWalk;
                        if (1.0f < angle) {
                            angle = 1.0f;
                        }
                    } else {
                        angle = frame;
                        stance = kStanceRun;
                    }
                } else {
                    angle = 1.0f;
                    stance = kStanceStill;
                }
            }
            if (recentre) {
                // The lean eases back to upright.
                if (0.0f < body.lean) {
                    body.lean -= kLeanStep;
                }
                if (body.lean < 0.0f) {
                    body.lean += kLeanStep;
                }
            }

            if (const MultiFiring* multi = combat_ ? combat_->Multi(group, stance) : nullptr) {
                firingTable = multi->entry;
                if (!multi->anim.empty()) {
                    anim = Clip(multi->anim);
                }
                angle *= multi->speedScale;
                endFrame = multi->endFrame;
                loopFrame = multi->loopFrame;
            }
            animSpeed = angle;
        }

        if (firingTable != nullptr && anim == nullptr) {
            anim = Clip(firingTable->anim);
        }

        if (anim != nullptr) {
            bool setAnim = anim != current;
            if (0.0f <= loopFrame) {
                if (!playback.animLooping || loopFrame != playback.animLoopFrame) {
                    setAnim = true;
                }
            }
            if (loopFrame < 0.0f && playback.animLooping) {
                setAnim = true;
            }

            if (setAnim) {
                if (playback.anim2 == nullptr) {
                    const float startFrame = 0.0f <= loopFrame ? loopFrame : 0.0f;
                    playback.SetAnimation(anim, false, startFrame, animSpeed, 16.0f);
                    body.clip = anim;
                    body.clipSpeed = animSpeed;
                    if (0.0f <= loopFrame) {
                        playback.SetAnimLooping(loopFrame, 16.0f);
                    }
                    if (0.0f <= endFrame) {
                        playback.SetAnimEndFrame(endFrame);
                    }
                }
            } else if (animSpeed != body.clipSpeed) {
                playback.SetAnimSpeed(animSpeed, 1.0f);
                body.clipSpeed = animSpeed;
            }
            current = body.clip;

            if (anim == current) {
                if (firingTable != nullptr) {
                    chr.hidden &= ~ChrHidden::Unnamed0x400;
                    UpdateAimEndShoulders(chr, firingTable, false, true, aimPitch);
                } else {
                    chr.aimEndRightShoulder = 0.0f;
                    chr.aimEndLeftShoulder = 0.0f;
                    chr.hidden |= ChrHidden::Unnamed0x400;
                    chr.aimEndBack = aimPitch;
                }
            }
        }

        chr.aimEndSideBack = aimYaw;
        chr.aimEndCount = kAimEndTicks;
    }

    prop.position = body.position;
    prop.stan = body.tile;

    playback.SetPosition({ prop.position[0], playback.root.pos.y, prop.position[2] });
    playback.SetHeading((((360.0f - body.theta) + body.lean) * kTau) / 360.0f);

    chr.chrflags |= ChrFlag::Init;
    chr.actionType = ChrAction::BondMulti;
    chr.bondMultiAnim = firingTable;

    SetFiring(chr, HandRight, body.firing[HandRight]);
    SetFiring(chr, HandLeft, body.firing[HandLeft]);
}

} // namespace Frigate::Level
