#include <algorithm>
#include <cmath>

#include "level/ChrActions.h"
#include "level/HatType.h"
#include "level/HitParts.h"
#include "level/WeaponTable.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;

constexpr float DegToRad(float degrees) {
    return degrees * kTau / 360.0f;
}

// How many characters one death tells about it (chrlvIterateGuardSeeShotDie).
constexpr int kMaxAlerted = 4;
// The squared distance within which a character sees another shot or die.
constexpr float kSeeShotRangeSquared = 4000000.0f;

// get_007_reaction_speed is 0 outside the 007 difficulty's sliders.

} // namespace

// -- Held weapons ---------------------------------------------------------------------

PlacedObject* ChrActions::HeldObject(const Chr& chr, int hand) const {
    const int prop = Held(chr, hand);
    return prop >= 0 ? scene_.objects->ForProp(prop) : nullptr;
}

void ChrActions::SetFiring(Chr& chr, int hand, bool firing) {
    // chrSetFiring: weaponSetGunfireVisible, which acts on the models that
    // have the weapon skeleton.
    PlacedObject* weapon = HeldObject(chr, hand);
    if (weapon == nullptr || !weapon->instance) {
        return;
    }
    const PropModel* model = scene_.objects->Model(weapon->model);
    if (model != nullptr && model->weaponSkeleton) {
        weapon->instance->SetGunfireVisible(kGunfireSlot, firing);
        weapon->instance->SetSwitchVisible(kWeaponFlashSlot, firing);
    }
}

void ChrActions::ResetAimEnd(Chr& chr) {
    // chrlvResetAimend.
    chr.aimEndCount = 0xA;
    chr.aimEndRightShoulder = 0.0f;
    chr.aimEndLeftShoulder = 0.0f;
    chr.aimEndBack = 0.0f;
    chr.aimEndSideBack = 0.0f;
}

void ChrActions::StopFiring(Chr& chr) {
    // chrStopFiring.
    SetFiring(chr, HandRight, false);
    SetFiring(chr, HandLeft, false);
    ResetAimEnd(chr);
}

void ChrActions::ToggleFiring(Chr& chr, int hand, bool fire) {
    // chrlvToggleHiddenRelated.
    const uint32_t bit = hand == HandLeft ? ChrHidden::FireWeaponLeft : ChrHidden::FireWeaponRight;
    if (fire) {
        chr.hidden |= bit;
    } else {
        chr.hidden &= ~bit;
        SetFiring(chr, hand, false);
    }
}

bool ChrActions::MaybeSameRoom(Chr& chr, const Vec3& position, int tile) {
    // chrlvMaybeSameRoom: not for a position behind the character's left.
    const Vec3& mine = PropOf(chr).position;
    const float heading = ChrHeading(chr);
    const float atan = std::atan2(position[0] - mine[0], position[2] - mine[2]);
    float angle = atan - heading;
    if (atan < heading) {
        angle += kTau;
    }
    if (angle < DegToRad(100.0f) || angle > DegToRad(260.0f)) {
        return ai_->PositionInSameRoom(chr, position, tile);
    }
    return false;
}

// -- Hit reactions ---------------------------------------------------------------------

float ChrActions::FlinchAmount(const Chr& chr) {
    // chrGetFlinchAmount.
    constexpr float rise = 8.0f;
    constexpr float fall = 16.0f;
    const float count = static_cast<float>(chr.flinchCount);
    if (count < rise) {
        return std::sin(((count * kTau) * 0.25f) / rise);
    }
    return 1.0f - std::sin((((count - rise) * kTau) * 0.25f) / fall);
}

void ChrActions::FlinchRandomly(Chr& chr) {
    // chrSetHiddenToRandom.
    if (chr.flinchCount >= 0) {
        return;
    }
    chr.flinchCount = 1;
    chr.hidden &= ~ChrHidden::RandFlinchMask;

    uint32_t pick = RandomGetNext() % 3u;
    if (pick == 0) {
        chr.hidden |= ChrHidden::RandFlinch1;
    } else if (pick == 1) {
        chr.hidden |= ChrHidden::RandFlinch2;
    }

    pick = RandomGetNext() % 3u;
    if (pick == 0) {
        chr.hidden |= ChrHidden::RandFlinch4;
    } else if (pick == 1) {
        chr.hidden |= ChrHidden::RandFlinch8;
    }
}

void ChrActions::UpdateAimProperties(Chr& chr) {
    // chrUpdateAimProperties.
    const ChrTiming& timing = scene_.chrs->Timing();
    if (chr.aimEndCount >= 2) {
        float mult = timing.globalTimerDelta / static_cast<float>(chr.aimEndCount);
        if (mult > 1.0f) {
            mult = 1.0f;
        }
        chr.aimUpLeftShoulder += (chr.aimEndLeftShoulder - chr.aimUpLeftShoulder) * mult;
        chr.aimUpRightShoulder += (chr.aimEndRightShoulder - chr.aimUpRightShoulder) * mult;
        chr.aimUpBack += (chr.aimEndBack - chr.aimUpBack) * mult;
        chr.aimSideBack += (chr.aimEndSideBack - chr.aimSideBack) * mult;
        chr.aimEndCount = static_cast<int8_t>(chr.aimEndCount - timing.clockTimer);
        return;
    }
    chr.aimUpLeftShoulder = chr.aimEndLeftShoulder;
    chr.aimUpRightShoulder = chr.aimEndRightShoulder;
    chr.aimUpBack = chr.aimEndBack;
    chr.aimSideBack = chr.aimEndSideBack;
}

bool ChrActions::HitChr(Chr& target, int part, const Vec3& direction, int item) {
    return HandleShot(target, part, direction, item, false);
}

bool ChrActions::HandleShot(Chr& chr, int hitpart, const Vec3& vector, int weapon, bool fromPlayer) {
    // handles_shot_actors.
    const Prop& prop = PropOf(chr);

    // A hat that is shot.
    if (hitpart == HitPart::Hat && chr.hat >= 0) {
        HatType hatType = HatType::Other;
        if (const PlacedObject* hat = scene_.objects->ForProp(chr.hat)) {
            if (const PropModel* model = scene_.objects->Model(hat->model)) {
                hatType = HatTypeOf(model->name);
            }
        }
        if (hatType == HatType::Moon) {
            // Counts as a head hit.
            hitpart = HitPart::Head;
        } else if (hatType != HatType::Helmet) {
            // The hat is knocked off.
            host_.DropProp(chr.hat, DropType::Hat);
            chr.hidden |= ChrHidden::DropHeldItems;
        } else {
            // A steel helmet ricochets the shot; the sound is picked with a draw.
            // TODO(https://github.com/liz-baker/frigate/issues/41): the ricochet sound.
            RandomGetNext();
        }
    }

    if (fromPlayer && hitpart != 0) {
        switch (hitpart) {
            case HitPart::Head:
                host_.CountPlayerShot(weapon, ShotRegister::Head);
                break;
            case HitPart::Gun:
                host_.CountPlayerShot(weapon, ShotRegister::Gun);
                break;
            case HitPart::Hat:
                host_.CountPlayerShot(weapon, ShotRegister::Hat);
                break;
            case HitPart::Chest:
            case HitPart::Pelvis:
                host_.CountPlayerShot(weapon, ShotRegister::Body);
                break;
            default:
                host_.CountPlayerShot(weapon, ShotRegister::Limb);
                break;
        }
    }

    chr.numArghs++;
    chr.chrflags |= ChrFlag::WasHit;

    // An invincible character flinches, and is done.
    if ((chr.chrflags & ChrFlag::Invincible) != 0) {
        FlinchRandomly(chr);
        return false;
    }

    // A character already dying or dead is done with.
    if (chr.actionType != ChrAction::Die && chr.actionType != ChrAction::Dead) {
        const WeaponTable* weapons = host_.Weapons();
        float damageToCause = weapons != nullptr ? weapons->Item(weapon).destructionAmount : 0.0f;
        if (fromPlayer) {
            damageToCause *= host_.AiHealth();
        }

        const Vec3 away = { prop.position[0] - vector[0], prop.position[1] - vector[1], prop.position[2] - vector[2] };
        const float angle = AngleTo(chr, away);

        if (hitpart == HitPart::General) {
            // Halved here because it is doubled for the torso below.
            hitpart = HitPart::Chest;
            damageToCause *= 0.5f;
        } else if (hitpart == HitPart::GeneralHalf) {
            hitpart = HitPart::Chest;
            damageToCause *= 0.25f;
        }

        if (weapon == kItemFist) {
            if (chr.actionType != ChrAction::Stand && chr.actionType != ChrAction::Patrol &&
                chr.actionType != ChrAction::Surrender && chr.actionType != ChrAction::Anim &&
                (chr.actionType != ChrAction::GoPos || chr.actGoPos.speedMode != 0)) {
                // Punching is less effective from the front.
                if (angle < DegToRad(60.0f) || angle > DegToRad(300.0f)) {
                    damageToCause *= 0.125f;
                } else if (angle < DegToRad(180.0f - 60.0f) || angle > DegToRad(180.0f + 60.0f)) {
                    damageToCause *= 0.25f;
                } else {
                    damageToCause *= 0.5f;
                }
            }
        }

        if (hitpart == HitPart::Head) {
            damageToCause *= 4.0f;
        } else if (hitpart == HitPart::Chest) {
            damageToCause *= 2.0f;
        } else if (hitpart == HitPart::Gun) {
            damageToCause = 0.0f;
        } else if (hitpart == HitPart::Hat) {
            damageToCause = 0.0f;
        }

        chr.chrflags |= ChrFlag::WasDamaged;
        chr.damage += damageToCause;

        if (chr.damage < 0.0f) {
            float endFrame = -1.0f;
            if (!HoldsForShot(chr, &endFrame)) {
                FlinchRandomly(chr);
                return false;
            }
        }

        if (hitpart != HitPart::Hat) {
            // Cancel the current animation and prepare for the reaction.
            float endFrame = -1.0f;
            // TODO(https://github.com/liz-baker/frigate/issues/41): play_sound_for_shot_actor.
            if (HoldsForShot(chr, &endFrame)) {
                if (endFrame >= 0.0f) {
                    EndAt(chr, endFrame);
                }
                chr.actionType = ChrAction::PreArgh;
                chr.actPreArgh.position = vector;
                chr.actPreArgh.angle = angle;
                chr.actPreArgh.hitPart = hitpart;
                chr.actPreArgh.weapon = weapon;
                chr.sleep = 0;
            } else {
                ShotReaction(chr, vector, angle, hitpart, weapon);
            }
        }
    }
    return true;
}

bool ChrActions::HoldsForShot(Chr& chr, float* endFrame) {
    // chrlvAttackAnimationRelated7F026F30: whether a roll is to be played out
    // before the character reacts to a shot, and the frame it plays to.
    bool hold = false;
    const ChrCombatTables& tables = *combat_;
    if (chr.actionType == ChrAction::AttackRoll) {
        const std::vector<FiringAnim>& rolls = tables.Rolls();
        const FiringAnim* anim = chr.actAttack.anim;
        if (chr.actAttack.dualRoll != 0) {
            const int index = ChrCombatTables::IndexIn(rolls, anim);
            if (index >= 4 && index <= 7) {
                float out = anim->turnEndFrame - 8.0f;
                if (anim->endFrame < anim->turnEndFrame) {
                    out = anim->endFrame;
                }
                if (AnimationFrame(chr) < out) {
                    *endFrame = out;
                    hold = true;
                }
            }
        } else {
            const float out = anim->turnEndFrame - 8.0f;
            if (AnimationFrame(chr) < out) {
                *endFrame = out;
                hold = true;
            }
        }
    } else if (chr.actionType == ChrAction::PreArgh) {
        // The decomp returns without setting the frame.
        hold = true;
    }
    return hold;
}

void ChrActions::ShotReaction(Chr& chr, const Vec3& direction, float angle, int requestedAnim, int item) {
    // triggered_on_shot_hit.
    const ChrCombatTables& tables = *combat_;
    bool dropHeld = true;
    bool reacted = false;
    int reactionIndex = 0;
    const Prop& prop = PropOf(chr);
    const ChrTiming& timing = scene_.chrs->Timing();

    const bool merge = !(chr.actionType == ChrAction::Argh && timing.globalTimer == chr.actArgh.hitTick);
    // flag1 in the decomp is the opposite: the merge is on when it is false.

    reactionIndex = tables.ReactionIndexFor(requestedAnim);

    if (chr.damage >= chr.maxDamage) {
        if ((angle < 1.5707964f || angle > 4.712389f) && (RandomGetNext() % 0x14u) == 0) {
            float behind = ChrHeading(chr) + kPi;

            float upper = behind + 0.17453294f;
            float lower = behind - 0.17453294f;
            if (upper >= kTau) {
                upper -= kTau;
            }
            if (lower >= kTau) {
                lower -= kTau;
            }

            const float over = ClearBehind(chr, upper, 150.0f);
            const float under = ClearBehind(chr, lower, 150.0f);

            if (over < 150.0f && under < 150.0f) {
                const float difference = over - under;
                if (difference < 10.0f && difference > -10.0f) {
                    const std::vector<StruckAnim>& stagger = tables.DeathStagger();
                    const StruckAnim& struck = stagger[RandomGetNext() & 1u];

                    StopFiring(chr);
                    chr.actionType = ChrAction::Die;
                    chr.actDie.notifyChrIndex = 0;
                    chr.actDie.thudFrame1 = struck.thudFrame1;
                    chr.actDie.thudFrame2 = struck.thudFrame2;
                    chr.sleep = 0;
                    chr.actDie.timeExtra = 0.0f;

                    AnimateMerge(chr, struck.anim, struck.flip != 0, 0.0f, struck.speed, 16.0f, merge);
                    if (struck.endFrame >= 0.0f) {
                        EndAt(chr, struck.endFrame);
                    }

                    // The character slumps against what is behind it.
                    chr.chrWidth = 10.0f;
                    reacted = true;
                }
            }
        }

        if (!reacted) {
            const HitReaction& reaction = tables.Reactions()[static_cast<size_t>(reactionIndex)];
            if (reaction.death != nullptr && !reaction.death->empty()) {
                reacted = true;

                const size_t pick = RandomGetNext() % static_cast<uint32_t>(reaction.death->size());
                const StruckAnim& struck = (*reaction.death)[pick];
                StopFiring(chr);

                chr.actionType = ChrAction::Die;
                chr.actDie.notifyChrIndex = 0;
                chr.actDie.thudFrame1 = struck.thudFrame1;
                chr.actDie.thudFrame2 = struck.thudFrame2;
                chr.sleep = 0;
                chr.actDie.timeExtra = 0.0f;

                AnimateMerge(chr, struck.anim, struck.flip != 0, 0.0f, struck.speed, 16.0f, merge);

                if (struck.anim == "death_neck" && (RandomGetNext() % 0x64u) != 0) {
                    EndAt(chr, 241.0f);
                } else if (struck.endFrame >= 0.0f) {
                    EndAt(chr, struck.endFrame);
                }

                float force = host_.Weapons() != nullptr ? tables.ForceOfImpact(item) : 0.0f;
                if (force <= 0.0f && (chr.chrflags & ChrFlag::ImpactAlways) != 0) {
                    force = 6.0f;
                }

                if (struck.knockback != 0 && force > 0.0f) {
                    chr.actDie.elapseExtra = 0.0f;
                    chr.actDie.timeExtra = (force * 90.0f) / 6.0f;
                    chr.actDie.extraSpeed = { direction[0] * force, direction[1] * force, direction[2] * force };
                }
            }
        }

        DropItems(chr);
        // increment_num_kills_display_text_in_MP is multiplayer's.
        if ((chr.chrflags & ChrFlag::CountDeathAsCivilian) != 0) {
            host_.CountCivilianCasualty();
        }
    } else {
        if (requestedAnim == HitPart::Pelvis && angle > 2.3561945f && angle < 3.926991f &&
            RandomGetNext() % 5u < 2u) {
            const uint32_t variant = RandomGetNext() % 5u;
            StopFiring(chr);
            chr.actionType = ChrAction::Argh;
            chr.actArgh.notifyChrIndex = 0;
            chr.actArgh.hitTick = timing.globalTimer;
            chr.sleep = 0;

            if ((RandomGetNext() & 1u) != 0) {
                const bool flip = (RandomGetNext() & 1u) != 0;
                AnimateMerge(chr, "hit_butt_long", flip, 10.0f, 0.5f, 16.0f, merge);
                if (variant < 2u) {
                    EndAt(chr, ArghRating(chr, 34.0f, 8.0f));
                } else if (variant < 4u) {
                    EndAt(chr, ArghRating(chr, 71.0f, 8.0f));
                } else {
                    EndAt(chr, ArghRating(chr, LastFrame(chr), 8.0f));
                }
            } else {
                const bool flip = (RandomGetNext() & 1u) != 0;
                AnimateMerge(chr, "hit_butt_short", flip, 0.0f, 0.5f, 16.0f, merge);
                if (variant < 2u) {
                    EndAt(chr, ArghRating(chr, 37.0f, 8.0f));
                } else if (variant < 4u) {
                    EndAt(chr, ArghRating(chr, 70.0f, 8.0f));
                } else {
                    EndAt(chr, ArghRating(chr, LastFrame(chr), 8.0f));
                }
            }
            reacted = true;
        }

        if (!reacted) {
            const HitReaction* reaction = &tables.Reactions()[static_cast<size_t>(reactionIndex)];
            if (reaction->flinch != nullptr && !reaction->flinch->empty()) {
                reacted = true;

                // A hit on the left or right hand flinches differently when the
                // other hand holds a gun.
                if (reactionIndex == 9 && Held(chr, HandLeft) >= 0) {
                    reactionIndex = 10;
                } else if (reactionIndex == 12 && Held(chr, HandRight) >= 0) {
                    reactionIndex = 13;
                }
                reaction = &tables.Reactions()[static_cast<size_t>(reactionIndex)];
                const size_t pick = RandomGetNext() % static_cast<uint32_t>(reaction->flinch->size());
                const StruckAnim& struck = (*reaction->flinch)[pick];

                StopFiring(chr);

                chr.actionType = ChrAction::Argh;
                chr.actArgh.notifyChrIndex = 0;
                chr.actArgh.hitTick = timing.globalTimer;
                chr.sleep = 0;

                AnimateMerge(chr, struck.anim, struck.flip != 0, 0.0f, struck.speed, 16.0f, merge);

                if (struck.endFrame >= 0.0f) {
                    EndAt(chr, ArghRating(chr, struck.endFrame, 8.0f));
                } else {
                    EndAt(chr, ArghRating(chr, LastFrame(chr), 8.0f));
                }
            }
        }

        dropHeld = false;
    }

    if (dropHeld && reacted) {
        // What the character held falls, unless it was told to keep it.
        for (const int hand : { HandRight, HandLeft }) {
            const PlacedObject* weapon = HeldObject(chr, hand);
            if (weapon != nullptr && (weapon->flags & ObjectFlag::AiUndroppable) == 0) {
                host_.DropProp(Held(chr, hand), DropType::Default);
                chr.hidden |= ChrHidden::DropHeldItems;
            }
        }
    }
    (void)prop;
}

float ChrActions::ArghRating(const Chr& chr, float min, float max) const {
    // chrlvGetGuard007ArghRating.
    float rating = static_cast<float>(chr.arghRating);
    rating = (ai_->ReactionSpeed() * (100.0f - rating)) + rating;
    return ((rating * (max - min)) / 100.0f) + min;
}

float ChrActions::ClearBehind(Chr& chr, float angle, float distance) {
    // chrlvPathingCollisionRelated7F0264B0.
    float width = 0.0f;
    float height = 0.0f;
    float always20 = 0.0f;
    Chrs::WidthHeight(chr, &width, &height, &always20);
    return ClearDistance(chr, angle, distance,
                         CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs | CdType::PathBlocker, height,
                         always20);
}

bool ChrActions::AnimateMerge(Chr& chr, const std::string& name, bool flip, float startFrame, float speed,
                              float merge, bool doMerge) {
    // modelSetAnimationWithMerge.
    const AnimationClip* clip = Clip(name);
    if (!chr.model || clip == nullptr) {
        return false;
    }
    chr.model->Playback().SetAnimationWithMerge(clip, flip, startFrame, speed, merge, doMerge);
    return true;
}

bool ChrActions::ExplosionDamage(Chr& chr, const Vec3& position, float damage, bool sound) {
    // chrlvExplosionDamage.
    if (chr.actionType == ChrAction::Dead || chr.actionType == ChrAction::Die) {
        return false;
    }

    chr.chrflags |= ChrFlag::WasHit;
    if ((chr.chrflags & ChrFlag::Invincible) != 0) {
        return false;
    }

    chr.numArghs += 1;
    chr.damage += damage;
    chr.chrflags |= ChrFlag::WasDamaged;

    if (chr.damage <= 0.0f) {
        return false;
    }
    chr.damage = chr.maxDamage;

    const Vec3& mine = PropOf(chr).position;
    const float heading = ChrHeading(chr);
    const float atan = std::atan2(mine[0] - position[0], mine[2] - position[2]);

    Vec3 away = { mine[0] - position[0], mine[1] - position[1], mine[2] - position[2] };
    // Avoid dividing by zero.
    if (away[0] == 0.0f && away[1] == 0.0f && away[2] == 0.0f) {
        away[2] = 1.0f;
    }

    const float norm =
        (5.0f * damage) / std::sqrt(((away[0] * away[0]) + (away[1] * away[1])) + (away[2] * away[2]));
    float relative = atan - heading;

    away[0] *= norm;
    away[1] *= norm;
    away[2] *= norm;

    chr.fallSpeed = away;

    if (atan < heading) {
        relative += kTau;
    }

    int direction = static_cast<int>(((relative * 8.0f) / kTau) + 0.5f);
    if (direction >= ChrCombatTables::kExplosionDirections) {
        direction = 0;
    }

    const ChrCombatTables& tables = *combat_;
    const std::vector<int>& group = tables.ExplosionGroup(direction);
    const size_t pick = RandomGetNext() % static_cast<uint32_t>(group.size());
    const ExplosionDeath& death = tables.ExplosionDeathAt(group[pick]);

    StopFiring(chr);

    chr.actionType = ChrAction::Die;
    chr.actDie.notifyChrIndex = 0;
    chr.actDie.thudFrame1 = death.thudFrame;
    chr.sleep = 0;
    chr.actDie.thudFrame2 = -1.0f;
    chr.actDie.timeExtra = 0.0f;

    Animate(chr, death.anim, death.flip != 0, death.startFrame, death.speed, 8.0f);

    if (death.endFrame >= 0.0f) {
        EndAt(chr, death.endFrame);
    }

    if (sound) {
        // TODO(https://github.com/liz-baker/frigate/issues/41): play_sound_for_shot_actor.
    }

    DropItems(chr);
    // increment_num_kills_display_text_in_MP is multiplayer's.
    if ((chr.chrflags & ChrFlag::CountDeathAsCivilian) != 0) {
        host_.CountCivilianCasualty();
    }

    for (const int hand : { HandRight, HandLeft }) {
        const PlacedObject* weapon = HeldObject(chr, hand);
        if (weapon != nullptr && (weapon->flags & ObjectFlag::AiUndroppable) == 0) {
            host_.DropProp(Held(chr, hand), DropType::Default);
            chr.hidden |= ChrHidden::DropHeldItems;
        }
    }
    return true;
}

void ChrActions::DropItems(Chr& chr) {
    // chrDropItems: what a character carries without holding it falls.
    host_.DropConcealedItems(chr);
}

// -- Ticks of the reactions -------------------------------------------------------------

void ChrActions::IterateSeeShotDie(Chr& chr, bool died) {
    // chrlvIterateGuardSeeShotDie.
    const auto& slots = scene_.chrs->Slots();
    const int count = static_cast<int>(slots.size());
    int index = 0;
    int alerted = 0;

    if (chr.actionType == ChrAction::Argh) {
        index = chr.actArgh.notifyChrIndex;
    } else if (chr.actionType == ChrAction::Die) {
        index = chr.actDie.notifyChrIndex;
    }

    const Prop& mine = PropOf(chr);
    for (; index < count && alerted < kMaxAlerted; index++) {
        Chr& guard = scene_.chrs->Slots()[static_cast<size_t>(index)];
        if (!guard.inUse || guard.prop < 0) {
            continue;
        }
        const Prop& theirs = PropOf(guard);
        const float dx = theirs.position[0] - mine.position[0];
        const float dy = theirs.position[1] - mine.position[1];
        const float dz = theirs.position[2] - mine.position[2];

        if (((dx * dx) + (dy * dy) + (dz * dz)) < kSeeShotRangeSquared) {
            alerted++;

            if (MaybeSameRoom(guard, mine.position, mine.stan)) {
                if (!died) {
                    guard.chrSeeShot = chr.chrnum;
                } else {
                    guard.chrSeeDie = chr.chrnum;
                }
            }
        }
    }

    if (chr.actionType == ChrAction::Argh) {
        chr.actArgh.notifyChrIndex = index;
    } else if (chr.actionType == ChrAction::Die) {
        chr.actDie.notifyChrIndex = index;
    }
}

void ChrActions::TickDie(Chr& chr) {
    // chrlvTickDie.
    const float frame = AnimationFrame(chr);

    // The thud of the body hitting the floor at each cue is a sound.
    // TODO(https://github.com/liz-baker/frigate/issues/41): the thuds.
    if (chr.actDie.thudFrame1 >= 0.0f && chr.actDie.thudFrame1 <= frame) {
        chr.actDie.thudFrame1 = -1.0f;
    }
    if (chr.actDie.thudFrame2 >= 0.0f && chr.actDie.thudFrame2 <= frame) {
        chr.actDie.thudFrame2 = -1.0f;
    }

    if (AnimationFrame(chr) >= AnimationLength(chr)) {
        if (IsPlaying(chr, "death_left_leg")) {
            // The fall carries on into a backwards jump.
            const AnimationClip* clip = Clip("jump_backwards");
            if (clip != nullptr && chr.model) {
                const float remaining = (static_cast<float>(clip->frameCount) - 1.0f) - 50.0f;
                Animate(chr, "jump_backwards", !Mirrored(chr), 50.0f, 0.3f, remaining);
                chr.model->Playback().SetAnimSpeed(0.5f, remaining);
            }
            return;
        }

        FadeOut(chr);
    }

    IterateSeeShotDie(chr, true);
}

void ChrActions::TickArgh(Chr& chr) {
    // chrlvTickArgh.
    if (AnimationFrame(chr) >= AnimationLength(chr)) {
        ai_->SetTargetToPlayer(chr);

        if (IsPlaying(chr, "death_left_leg")) {
            MergeToStandFacingWalls(chr, 26.0f);
        } else {
            StandUpFacingWalls(chr);
        }
    }

    IterateSeeShotDie(chr, false);
}

void ChrActions::TickPreArgh(Chr& chr) {
    // chrlvTickPreArgh.
    if (AnimationFrame(chr) >= AnimationLength(chr)) {
        ShotReaction(chr, chr.actPreArgh.position, chr.actPreArgh.angle, chr.actPreArgh.hitPart,
                     chr.actPreArgh.weapon);
    }
}

void ChrActions::TickSurrender(Chr& chr) {
    // chrlvTickSurrender.
    if (chr.sleep <= 0) {
        chr.sleep = 0x10;

        if (IsPlaying(chr, "surrendering_armed_drop_weapon") && AnimationFrame(chr) >= 80.0f) {
            const float heading = ChrHeading(chr);
            const Vec3 direction = { -std::sin(heading), 0.0f, -std::cos(heading) };

            // The character stays on its feet only while it has room behind it.
            if (!CircleClear(chr, direction, 20.0f)) {
                const bool mirror = (RandomGetNext() & 1u) != 0;
                Animate(chr, "surrendering_armed", mirror, 30.0f, 0.5f, 16.0f);
                Loop(chr, 30.0f, 16.0f);
            }
        }
    }
}

bool ChrActions::CircleClear(Chr& chr, const Vec3& direction, float distance) {
    // chrlvCall7F02982C.
    const Vec3& mine = PropOf(chr).position;
    const Vec3 to = { mine[0] + (direction[0] * distance), mine[1], mine[2] + (direction[2] * distance) };
    return SidestepClear(chr, to, direction);
}

// -- Surrendering and throwing ---------------------------------------------------------

void ChrActions::Surrender(Chr& chr) {
    // chrlvActorThrowWeaponSurrender.
    if (chr.actionType == ChrAction::Surrender) {
        return;
    }
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);

    StopFiring(chr);

    chr.actionType = ChrAction::Surrender;

    if (right >= 0 || left >= 0) {
        const bool mirror = (RandomGetNext() & 1u) != 0;
        Animate(chr, "surrendering_armed_drop_weapon", mirror, 0.0f, 0.5f, 16.0f);
        Loop(chr, 40.0f, 16.0f);

        chr.sleep = 0x10;

        if (left >= 0) {
            host_.DropProp(left, DropType::Surrender);
        }
        if (right >= 0) {
            host_.DropProp(right, DropType::Surrender);
        }

        chr.hidden |= ChrHidden::DropHeldItems;
    } else {
        const bool mirror = (RandomGetNext() & 1u) != 0;
        Animate(chr, "surrendering_armed", mirror, 0.0f, 0.5f, 16.0f);
        Loop(chr, 30.0f, 16.0f);

        chr.sleep = 0x10;
    }

    DropItems(chr);
}

void ChrActions::StartThrowGrenade(Chr& chr, int hand, bool fromStart) {
    // chrlvThrowGrenade.
    StopFiring(chr);

    chr.actionType = ChrAction::ThrowGrenade;
    chr.sleep = 0;

    if (fromStart) {
        Animate(chr, "fire_throw_grenade", hand != 0, 0.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
    } else {
        Animate(chr, "fire_throw_grenade", hand != 0, 84.0f, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);
    }

    EndAt(chr, 193.0f);
}

bool ChrActions::ThrowGrenade(Chr& chr) {
    // actor_draws_throws_grenade_at_player_if_possible.
    if ((RandomGetNext() % 0xFFu) >= chr.grenadeProb) {
        return false;
    }

    if (ai_->DistanceToBond(chr) < 10.0f) {
        return false;
    }

    if (!ai_->IsNotDeadOrShot(chr)) {
        return false;
    }

    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);

    if (right >= 0) {
        const PlacedObject* weapon = scene_.objects->ForProp(right);
        if (weapon != nullptr && weapon->weaponNum == kItemGrenade) {
            StartThrowGrenade(chr, HandRight, false);
            return true;
        }
    }

    if (left >= 0) {
        const PlacedObject* weapon = scene_.objects->ForProp(left);
        if (weapon != nullptr && weapon->weaponNum == kItemGrenade) {
            StartThrowGrenade(chr, HandLeft, false);
            return true;
        }
    }

    if (left < 0 || right < 0) {
        uint32_t flags = 0;
        if (right >= 0) {
            flags = 0x10000000;
        }

        // The grenade the character takes out is the pickup model 0xC4.
        constexpr int kGrenadeModel = 0xC4;
        if (host_.GiveWeapon(chr, kGrenadeModel, kItemGrenade, flags)) {
            const int hand = right < 0 ? HandRight : HandLeft;
            if (PlacedObject* grenade = HeldObject(chr, hand)) {
                // The grenade stays out of sight until the throw.
                grenade->runtimeFlags |= ObjectRuntime::Unnamed0x800;
            }
            StartThrowGrenade(chr, hand, true);
            return true;
        }
    }

    return false;
}

void ChrActions::TickThrowGrenade(Chr& chr) {
    // chrlvTickThrowGrenade.
    const float frame = AnimationFrame(chr);
    const int hand = Mirrored(chr) ? HandLeft : HandRight;
    PlacedObject* weapon = HeldObject(chr, hand);

    if (frame >= 20.0f && weapon != nullptr) {
        weapon->runtimeFlags &= ~ObjectRuntime::Unnamed0x800;
    }

    if (frame >= 61.0f && weapon != nullptr) {
        weapon->weaponTimer = static_cast<int16_t>(kDefaultTimer);
    }

    if (frame >= 119.0f && weapon != nullptr) {
        host_.DropProp(Held(chr, hand), DropType::ThrowGrenade);
        chr.hidden |= ChrHidden::DropHeldItems;
    }

    if (AnimationFrame(chr) >= AnimationLength(chr)) {
        StandUpFacingWalls(chr);
        return;
    }

    if (frame >= 87.0f && frame <= 110.0f) {
        TurnToFace(chr, 1, 110.0f, ai_->SpeedRating(chr, 1.0f, 1.6f), 0.0f);
    }
}

void ChrActions::TickBondIntro(Chr& chr) {
    // chrlvTickBondIntro.
    const float frame = AnimationFrame(chr);

    if (frame < 86.0f && AnimationLength(chr) <= frame) {
        if (chr.model) {
            Animate(chr, "fire_standing_draw_one_handed_weapon_fast", false, 86.0f, chr.model->Playback().speed, 24.0f);
            EndAt(chr, 131.0f);
        }
        return;
    }

    if (AnimationLength(chr) <= frame) {
        StandUp(chr);
    }
}

} // namespace Frigate::Level
