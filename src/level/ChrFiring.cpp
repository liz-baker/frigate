#include <algorithm>
#include <cmath>

#include "game/Sound.h"
#include "level/ChrActions.h"
#include "level/Beams.h"
#include "level/Matrix.h"
#include "level/WeaponTable.h"
#include "model/Random.h"
#include "model/GameMath.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;

// TARGET_*.
constexpr int kTargetBond = 0x01;
constexpr int kTargetFrontOfChr = 0x02;
constexpr int kTargetChr = 0x04;
constexpr int kTargetPad = 0x08;
constexpr int kTargetAimOnly = 0x20;
constexpr int kTargetDontTurn = 0x40;

// CHRLV_SEEN_RECENT_CHECK, CHRLV_LASTMOVEOK60_CHECK and the attack-walk timings
// (NTSC).
constexpr int kSeenRecentCheck = 120;
constexpr int kLastMoveOkCheck = 60;
constexpr int kWalkTimerMin = 0x15;
constexpr int kWalkTimerMax = 0x1F;
constexpr int kWalkShotDelayA = 0x5A;
constexpr int kWalkShotDelayB = 0x14;
constexpr int kWalkShotDelayC = 0xB4;
constexpr int kWalkShotDelayD = 0x28;

// M_U16_MAX_VALUE_F: how far a character's shot reaches.
constexpr float kShotReach = 65536.0f;

} // namespace

// -- Shots at the player -------------------------------------------------------------------

void ChrActions::UpdateShotBondSum(Chr& chr, bool* aimedAt, bool* hurt, int item) {
    // chrlvUpdateShotbondsum.
    const Prop& player = (*scene_.props)[host_.Player().prop];
    const Prop& mine = PropOf(chr);

    const float dx = player.position[0] - mine.position[0];
    const float dy = player.position[1] - mine.position[1];
    const float dz = player.position[2] - mine.position[2];

    const float atan = GameAtan2f(dx, dz);
    const float subroty = SubrotySideback(chr);
    float relative = atan - subroty;
    const float squared = (dx * dx) + (dy * dy) + (dz * dz);

    const float limit = AimLimitAngle(squared);

    if (relative < 0.0f) {
        relative += kTau;
    }

    bool within = relative < limit;
    if (!within) {
        within = (kTau - limit) < relative;
    }

    *aimedAt = within;
    *hurt = false;

    if (!host_.PlayerDamageShowing() && within) {
        const float distance = std::sqrt(squared);

        float rate = 0.16f * scene_.chrs->Timing().globalTimerDelta;

        if (distance > 300.0f) {
            rate *= (300.0f / distance);
        }

        if (chr.accuracyRating > 0) {
            rate *= (1.0f + (static_cast<float>(chr.accuracyRating) / 10.0f));
        } else if (chr.accuracyRating < 0) {
            if (chr.accuracyRating < -0x63) {
                rate = 0.0f;
            } else {
                rate *= (static_cast<float>(chr.accuracyRating + 0x64) / 100.0f);
            }
        }

        const float accuracyMod = host_.Ai007AccuracyMod();
        if (accuracyMod <= 1.0f) {
            rate *= accuracyMod;
        } else {
            rate *= (9.0f / (10.001f - accuracyMod));
        }

        rate *= host_.AiAccuracy();

        const WeaponTable* weapons = host_.Weapons();
        if (weapons != nullptr && weapons->Item(item).automaticFiringRate <= 0) {
            rate *= 2.0f;
        }

        if (item == kItemShotgun || item == kItemAutoShotgun) {
            rate *= 2.0f;
        }

        chr.shotBondSum += rate;

        if (chr.shotBondSum >= 1.0f) {
            const float destruction = weapons != nullptr ? weapons->Item(item).destructionAmount : 0.0f;
            float damage = (0.125f * destruction * host_.AiDamage()) * host_.Ai007DamageMod();

            if (item == kItemShotgun || item == kItemAutoShotgun) {
                damage *= 3.0f;
            }

            host_.DamagePlayer(damage, subroty);

            chr.shotBondSum = 0.0f;

            if (host_.PlayerDamageShowing()) {
                *hurt = true;
            }
        }
    }
}

bool ChrActions::ShotReachesTarget(Chr& chr, const Vec3& from, int fromTile) {
    // chrlvAttackRelated7F0292A8.
    bool reaches = false;
    int flags = kTargetBond;

    if (chr.actionType == ChrAction::Attack) {
        flags = chr.actAttack.attackType;
    }

    if ((flags & kTargetFrontOfChr) != 0) {
        return true;
    }

    int stan = fromTile;
    int targetTile = kNoTile;
    const Vec3 target = TargetLocation(chr, flags, chr.actAttack.entityId, &targetTile);
    ChrAi::SetMoving(chr, false);

    if ((flags & 1) != 0) {
        host_.SetPlayerCollides(false);
        if (host_.Player().visibleToGuards) {
            if (scene_.stan->TestLineUnobstructed(&stan, from[0], from[2], target[0], target[2],
                                                  CdType::Objs | CdType::Doors | CdType::Chrs | CdType::PathBlocker |
                                                      CdType::AiOpaque,
                                                  from[1], from[1], target[1], target[1]) != 0 &&
                stan == targetTile) {
                chr.seenBondTime = scene_.chrs->Timing().globalTimer;
                reaches = true;
            }
        }
        host_.SetPlayerCollides(true);
    } else if ((flags & 4) != 0) {
        if (scene_.stan->TestLineUnobstructed(&stan, from[0], from[2], target[0], target[2],
                                              CdType::Objs | CdType::Doors | CdType::Players | CdType::PathBlocker |
                                                  CdType::AiOpaque,
                                              from[1], from[1], target[1], target[1]) != 0 &&
            stan == targetTile) {
            reaches = true;
        }
    } else if ((flags & 8) != 0) {
        if (scene_.stan->TestLineUnobstructed(&stan, from[0], from[2], target[0], target[2],
                                              CdType::Objs | CdType::Doors | CdType::Players | CdType::Chrs |
                                                  CdType::PathBlocker | CdType::AiOpaque,
                                              from[1], from[1], target[1], target[1]) != 0 &&
            stan == targetTile) {
            reaches = true;
        }
    }

    ChrAi::SetMoving(chr, true);
    return reaches;
}

Vec3 ChrActions::EdgeIntersection(const Vec3& point, const Vec3& direction) const {
    // chrlvStanLineDirIntersection.
    Vec3 edgeA{};
    Vec3 edgeB{};
    scene_.stan->CollisionEdge(&edgeA, &edgeB);
    return LineLineIntersection(edgeA, edgeB, point, direction);
}

// -- Firing --------------------------------------------------------------------------------

void ChrActions::TriggerFireWeapon(Chr& chr) {
    // chrlvTriggerFireWeapon.
    chr.hidden &= ~ChrHidden::FireTracer;

    if ((chr.hidden & ChrHidden::FireWeaponRight) != 0) {
        FireWeapon(chr, HandRight);
        chr.hidden &= ~ChrHidden::FireWeaponRight;
    }

    if ((chr.hidden & ChrHidden::FireWeaponLeft) != 0) {
        FireWeapon(chr, HandLeft);
        chr.hidden &= ~ChrHidden::FireWeaponLeft;
    }
}

void ChrActions::PlayFireSound(Chr& chr, int hand, int item, bool shooting) {
    const WeaponTable* weapons = host_.Weapons();
    const int rate = weapons != nullptr ? weapons->Item(item).soundTriggerRate : 0;
    const int sound = weapons != nullptr ? weapons->Item(item).sound : 0;
    const int globalTimer = scene_.chrs->Timing().globalTimer;
    const size_t slot = static_cast<size_t>(hand);

    bool plays = false;
    if (shooting) {
        if (rate > 0) {
            // CHRHIDDEN_FIRE_TRACER is set when a shot has sounded, and keeps the
            // character's other hand quiet for the rest of the tick.
            plays = (chr.hidden & ChrHidden::FireTracer) == 0 && chr.gunSoundTime[slot] < globalTimer;
        } else {
            plays = true;
        }
    }

    if (!plays) {
        return;
    }
    Game::Sound::Handle& first = chr.gunSounds[slot][0];
    Game::Sound::Handle& second = chr.gunSounds[slot][1];
    first.StopIfPlaying();
    second.StopIfPlaying();

    if (sound != 0) {
        Game::Sound::Handle* handle = nullptr;
        if (first.IsNull()) {
            handle = &first;
        } else if (second.IsNull()) {
            handle = &second;
        }
        if (handle != nullptr) {
            Game::Sound::PlaySfx(static_cast<int16_t>(sound), handle->Pending());
            Game::Sound::PostVolumeAtPositionDefault(handle->Get(), PropOf(chr).position);
            chr.gunSoundTime[slot] = globalTimer + rate;
            chr.hidden |= ChrHidden::FireTracer;
        }
    }
}

void ChrActions::FireWeapon(Chr& chr, int hand) {
    // chrlvFireWeaponRelated.
    const Prop& mine = PropOf(chr);
    PlacedObject* weapon = HeldObject(chr, hand);
    if (weapon == nullptr) {
        return;
    }

    bool fired = false;
    bool waiting = false;
    const int item = weapon->weaponNum;
    const AiPlayer playerState = host_.Player();
    const Prop& player = (*scene_.props)[playerState.prop];
    const WeaponTable* weapons = host_.Weapons();
    const ChrTiming& timing = scene_.chrs->Timing();

    int attackType = 1;
    if (chr.actionType == ChrAction::Attack) {
        attackType = chr.actAttack.attackType;
    }

    const bool atBond = (attackType & 1) != 0;
    const int rate = weapons != nullptr ? weapons->Item(item).automaticFiringRate : 0;

    if (!atBond || chr.seenBondTime >= (timing.globalTimer - kSeenRecentCheck) || rate < 0) {
        bool shoots = false;
        bool beam = false;

        chr.fireCount[static_cast<size_t>(hand)]++;

        if (rate < 0) {
            shoots = true;
            beam = true;
        } else if ((static_cast<int>(chr.fireCount[static_cast<size_t>(hand)]) % rate) == 0) {
            shoots = true;

            if ((static_cast<int>(chr.fireCount[static_cast<size_t>(hand)]) % (rate * 2)) == 0 || item == kItemLaser) {
                beam = true;
            }
        } else {
            waiting = true;
        }

        if (shoots) {
            int hitTile = kNoTile;
            const float subroty = SubrotySideback(chr);
            const float elevation = AimUpBackShoulder(chr);
            int stan = mine.stan;
            fired = true;

            Vec3 origin{};
            if (!MuzzleMatrixPosition(chr, hand, host_.ViewToWorld(), &origin)) {
                origin = { mine.position[0], mine.position[1] + 30.0f, mine.position[2] };

                if (hand == 1) {
                    origin[0] += GameCosf(subroty) * 10.0f;
                    origin[2] += -GameSinf(subroty) * 10.0f;
                } else {
                    origin[0] += -GameCosf(subroty) * 10.0f;
                    origin[2] += GameSinf(subroty) * 10.0f;
                }
            }

            int originTile = kNoTile;
            if (scene_.stan->TestLineUnobstructed(&stan, mine.position[0], mine.position[2], origin[0], origin[2],
                                                  CdType::Doors, origin[1] - chr.ground, origin[1] - chr.ground, 0.0f,
                                                  1.0f) != 0) {
                originTile = stan;
            } else {
                chr.fireCount[static_cast<size_t>(hand)]--;
                fired = false;
            }

            if (fired) {
                bool aimedAt = false;
                bool hurt = false;
                bool spark = true;

                const bool reaches = ShotReachesTarget(chr, origin, originTile);

                const Vec3 direction = { GameCosf(elevation) * GameSinf(subroty), GameSinf(elevation),
                                         GameCosf(elevation) * GameCosf(subroty) };

                Vec3 end = { origin[0] + (direction[0] * kShotReach), origin[1] + (direction[1] * kShotReach),
                             origin[2] + (direction[2] * kShotReach) };

                ChrAi::SetMoving(chr, false);
                scene_.stan->ResetHits();
                stan = originTile;

                if (scene_.stan->TestLineUnobstructed(&stan, origin[0], origin[2], end[0], end[2],
                                                      CdType::Objs | CdType::Doors | CdType::Chrs | CdType::PathBlocker,
                                                      origin[1], origin[1], end[1], end[1]) == 0) {
                    end = EdgeIntersection(origin, direction);
                    hitTile = stan;
                    end[0] -= 26.0f * direction[0];
                    end[1] -= 26.0f * direction[1];
                    end[2] -= 26.0f * direction[2];
                }

                ChrAi::SetMoving(chr, true);

                float dx = end[0] - origin[0];
                float dy = end[1] - origin[1];
                float dz = end[2] - origin[2];

                const float reachSquared = (dx * dx) + (dy * dy) + (dz * dz);

                if (item == kItemRocketLauncher) {
                    if (((dx * dx) + (dy * dy) + (dz * dz)) > 160000.0f) {
                        host_.LaunchRound(chr, item, origin, originTile, direction, elevation, subroty);
                    } else {
                        fired = false;
                    }
                } else if (item == kItemGrenadeLauncher) {
                    if (((dx * dx) + (dy * dy) + (dz * dz)) > 160000.0f) {
                        host_.LaunchRound(chr, item, origin, originTile, direction, elevation, subroty);
                    } else {
                        fired = false;
                    }
                } else {
                    if (atBond && reaches) {
                        dx = (player.position[0] - origin[0]) - (direction[0] * 15.0f);
                        dy = (player.position[1] - origin[1]) - (direction[1] * 15.0f);
                        dz = (player.position[2] - origin[2]) - (direction[2] * 15.0f);

                        if (((dx * dx) + (dy * dy) + (dz * dz)) <= reachSquared) {
                            UpdateShotBondSum(chr, &aimedAt, &hurt, item);
                            spark = !hurt;

                            if (aimedAt &&
                                (chr.actionType == ChrAction::Attack || chr.actionType == ChrAction::AttackRoll)) {
                                chr.actAttack.attackTime = timing.globalTimer;
                            }
                        }
                    } else {
                        if (chr.actionType == ChrAction::Attack || chr.actionType == ChrAction::AttackRoll) {
                            chr.actAttack.attackTime = timing.globalTimer;
                        }
                    }

                    const Stan::SavedCollision& collision = scene_.stan->LastCollision();
                    const Prop* struck = collision.prop >= 0 ? &(*scene_.props)[collision.prop] : nullptr;

                    if (hurt) {
                        end = player.position;
                        hitTile = player.stan;
                        host_.PropHitSound(item, player.position, true);
                    } else {
                        if ((struck == nullptr ||
                             (struck->type != PropType::Chr && struck->type != PropType::Viewer)) &&
                            reachSquared < 10000.0f) {
                            spark = false;
                        }
                    }

                    if (spark) {
                        if (hitTile != kNoTile) {
                            host_.CreateSpark(end, 1, 26.0f, scene_.stan->TileRoom(hitTile));
                        }

                        if (struck != nullptr) {
                            host_.PropHitSound(item, struck->position,
                                               struck->type == PropType::Chr || struck->type == PropType::Viewer);

                            if (struck->type == PropType::Chr) {
                                if ((chr.chrflags & ChrFlag::CanShootChrs) != 0) {
                                    HandleShot((*scene_.chrs)[struck->object], 0xF, direction, item, false);
                                }
                            } else if (struck->type == PropType::Obj || struck->type == PropType::Weapon) {
                                if (PlacedObject* object = scene_.objects->ForProp(collision.prop)) {
                                    host_.ShootObject(*object,
                                                      weapons != nullptr ? weapons->Item(item).destructionAmount : 0.0f,
                                                      end, item);
                                }
                            }
                        } else {
                            host_.WorldHitSound(item, end);
                        }
                    }

                    if (beam) {
                        switch (item) {
                            case kItemPp7:
                            case kItemPp7Silenced:
                            case kItemDd44:
                            case kItemKlobb:
                            case kItemKf7:
                            case kItemZmg:
                            case kItemD5k:
                            case kItemD5kSilenced:
                            case kItemPhantom:
                            case kItemAr33:
                            case kItemRcp90:
                            case kItemRuger:
                            case kItemGoldenGun:
                            case kItemSilverPp7:
                            case kItemGoldPp7:
                            case kItemLaser:
                                StartBeam(&chr.beams[static_cast<size_t>(hand)], item, origin, end, RandomGetNext);
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
        }

        PlayFireSound(chr, hand, item, fired || waiting);
    }

    SetFiring(chr, hand, fired);
}

// -- Ticks of the attacks --------------------------------------------------------------------

void ChrActions::RestartAnimation(Chr& chr, float startFrame, float speed, float merge) {
    // modelSetAnimation with the clip that is playing.
    if (chr.model) {
        ModelPlayback& playback = chr.model->Playback();
        playback.SetAnimation(playback.anim, playback.mirror, startFrame, speed, merge);
    }
}

bool ChrActions::FinishRollAnimation(Chr& chr) {
    // chrlvAttackrollAnimationRelated7F02E2E0: a roll in a slower animation
    // carries on in the first one.
    const std::vector<FiringAnim>& rolls = combat_->Rolls();
    const int index = ChrCombatTables::IndexIn(rolls, chr.actAttack.anim);
    if (index == 2 || index == 3) {
        const bool mirror = Mirrored(chr);
        chr.actAttack.turnState = 2;
        chr.actAttack.anim = &rolls[1];
        chr.sleep = 0;

        const FiringAnim& next = rolls[1];
        Animate(chr, next.anim, mirror, next.shootEndFrame, ai_->SpeedRating(chr, 0.7f, 1.12f), 22.0f);

        if (rolls[1].endFrame >= 0.0f) {
            EndAt(chr, rolls[1].endFrame);
        }
        return true;
    }
    return false;
}

void ChrActions::RecoverFromRoll(Chr& chr) {
    // chrlvAttackrollAnimationRelated7F02E3B8.
    const FiringAnim& anim = *chr.actAttack.anim;
    if (anim.recoilEndFrame > 0.0f) {
        RestartAnimation(chr, anim.recoilEndFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 8.0f);
    } else {
        RestartAnimation(chr, anim.shootEndFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 8.0f);
    }

    if (anim.endFrame >= 0.0f) {
        EndAt(chr, anim.endFrame);
    }
}

void ChrActions::TickAttackCommon(Chr& chr) {
    // chrlvTickAttackCommon.
    Chr::ActAttack& attack = chr.actAttack;
    float frame = AnimationFrame(chr);
    const ChrTiming& timing = scene_.chrs->Timing();

    if (attack.attackTime < (attack.lastShotTime - 30) && chr.model->Playback().anim2 == nullptr) {
        if ((attack.anim->shootStartFrame + 10.0f) < frame && frame < attack.anim->shootEndFrame) {
            if (attack.anim->recoilEndFrame < 0.0f || frame < attack.anim->recoilEndFrame) {
                if (attack.allSingleShot == 0) {
                    if (!FinishRollAnimation(chr)) {
                        RestartAnimation(chr, attack.anim->shootEndFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 8.0f);

                        if (attack.anim->endFrame >= 0.0f) {
                            EndAt(chr, attack.anim->endFrame);
                        }
                    }
                } else {
                    RecoverFromRoll(chr);
                }

                attack.shotsFired = static_cast<int8_t>(attack.shotsPlanned + 1);
                frame = AnimationFrame(chr);
            }
        }
    }

    if (AnimationLength(chr) <= frame) {
        if (attack.anyAutomatic != 0 || attack.shotsPlanned < attack.shotsFired) {
            if (!FinishRollAnimation(chr)) {
                if ((attack.attackType & kTargetBond) != 0) {
                    ai_->SetTargetToPlayer(chr);
                }

                StandUpFacingWalls(chr);

                return;
            }
        } else if (attack.shotsFired == attack.shotsPlanned) {
            attack.shotsFired++;
            RecoverFromRoll(chr);
        } else if (attack.shotDue != 0) {
            float speed = 0.5f;
            float start;
            float end;

            if (attack.allSingleShot != 0) {
                if (attack.anim->recoilStartFrame > 0.0f) {
                    start = attack.anim->recoilStartFrame;
                } else {
                    start = attack.anim->shootStartFrame;
                }

                if (attack.anim->recoilEndFrame > 0.0f) {
                    end = attack.anim->recoilEndFrame;
                } else {
                    end = attack.anim->shootEndFrame;
                }
            } else {
                start = attack.anim->shootStartFrame;

                if (attack.anim->recoilStartFrame > 0.0f) {
                    end = attack.anim->recoilStartFrame;
                } else {
                    end = attack.anim->shootEndFrame;
                }
            }

            const float difference = end - start;
            if (difference < 12.0f) {
                speed = (difference * 0.5f) / 12.0f;
            } else if (difference > 16.0f) {
                speed = difference * 0.5f * 0.0625f;
            }

            if (attack.single[0] != 0 && attack.single[1] != 0) {
                speed = 2.0f * speed;
            }

            attack.shotDue = 0;

            RestartAnimation(chr, start, speed, 8.0f);
            EndAt(chr, end);
        }

        frame = AnimationFrame(chr);
    }

    if ((attack.attackType & kTargetDontTurn) == 0) {
        float offset = attack.anim->angleOffset;
        float limit = attack.anim->turnEndFrame;

        if ((attack.attackType & kTargetAimOnly) != 0) {
            if (AnimationLength(chr) < limit) {
                limit = AnimationLength(chr);
            }
        }

        if (Mirrored(chr)) {
            offset = kTau - offset;
        }

        attack.turnState =
            static_cast<int8_t>(TurnToFace(chr, attack.turnState, limit, ai_->SpeedRating(chr, 1.0f, 1.6f), offset));
    }

    if (attack.anim->aimStartFrame < frame && frame < attack.anim->aimEndFrame) {
        UpdateAimEndSideBack(chr, attack.anim, attack.fires[1] != 0, attack.fires[0] != 0, 1.0f);
    } else {
        ResetAimEnd(chr);
    }

    for (int i = 0; i < 2; i++) {
        const size_t hand = static_cast<size_t>(i);
        if (attack.fires[hand] != 0) {
            if (attack.single[hand] == 0) {
                if (attack.anim->shootStartFrame <= frame && frame < attack.anim->shootEndFrame) {
                    ToggleFiring(chr, i, true);
                    attack.lastShotTime = timing.globalTimer;

                    if (chr.actionType == ChrAction::AttackRoll) {
                        const float duration = attack.anim->shootEndFrame - attack.anim->shootStartFrame;

                        if (duration < 30.0f) {
                            if (attack.slowedTicks >= (60 - (static_cast<int32_t>(duration) * 2))) {
                                chr.model->Playback().SetAnimSpeed(0.5f, 0.0f);
                            } else {
                                chr.model->Playback().SetAnimSpeed(0.1f, 0.0f);
                                attack.slowedTicks += timing.clockTimer;
                            }
                        } else {
                            chr.model->Playback().SetAnimSpeed(0.5f, 0.0f);
                        }
                    } else {
                        chr.model->Playback().SetAnimSpeed(0.5f, 0.0f);
                    }
                } else {
                    ToggleFiring(chr, i, false);
                    chr.model->Playback().SetAnimSpeed(ai_->SpeedRating(chr, 0.5f, 0.8f), 0.0f);
                }
            } else if (attack.shotDue == 0 &&
                       (i == attack.nextHand || attack.single[static_cast<size_t>(attack.nextHand)] == 0) &&
                       ((attack.anim->recoilStartFrame >= 0.0f && attack.anim->recoilStartFrame <= frame &&
                         frame <= attack.anim->recoilEndFrame) ||
                        (attack.anim->recoilStartFrame < 0.0f && attack.anim->shootStartFrame <= frame))) {
                attack.shotDue = 1;
                attack.nextHand = static_cast<int8_t>(1 - attack.nextHand);
                attack.shotsFired++;
                attack.lastShotTime = timing.globalTimer;

                ToggleFiring(chr, i, true);
            } else {
                ToggleFiring(chr, i, false);
            }
        } else {
            ToggleFiring(chr, i, false);
        }
    }
}

void ChrActions::TickAttack(Chr& chr) {
    // chrlvTickAttack.
    Chr::ActAttack& attack = chr.actAttack;
    const float frame = AnimationFrame(chr);

    if (attack.typeOfMotion != 0) {
        if (attack.typeOfMotion == 1) {
            float start;
            if (attack.anim->recoilEndFrame >= 0.0f) {
                start = attack.anim->recoilEndFrame;
            } else {
                start = attack.anim->shootEndFrame;
            }

            RestartAnimation(chr, start, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);

            if (attack.anim->endFrame >= 0.0f) {
                EndAt(chr, attack.anim->endFrame);
            }

            attack.typeOfMotion = 2;
            ResetAimEnd(chr);

            return;
        }

        if (attack.typeOfMotion == 2) {
            if (AnimationLength(chr) <= frame) {
                attack.attackType |= kTargetAimOnly;
                attack.attackType &= ~kTargetDontTurn;

                if (attack.standing != 0) {
                    AttackFrom(chr, attack.attackType, attack.entityId, false);
                    return;
                }

                AttackFrom(chr, attack.attackType, attack.entityId, true);
                return;
            }

            return;
        }
    }

    if ((attack.attackType & kTargetAimOnly) != 0) {
        if ((attack.attackType & kTargetDontTurn) != 0) {
            if (!UpdateAimEndSideBack(chr, attack.anim, attack.fires[1] != 0, attack.fires[0] != 0, 0.2f)) {
                attack.typeOfMotion = 1;
            }

            return;
        }

        if (AnimationLength(chr) <= frame) {
            attack.attackType |= kTargetDontTurn;
            attack.turnState = 2;

            return;
        }
    }

    if (attack.allSingleShot == 0) {
        if (attack.anim->recoilEndFrame > 0.0f && frame <= attack.anim->recoilEndFrame) {
            if (AnimationLength(chr) <= frame) {
                RestartAnimation(chr, attack.anim->recoilEndFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);

                if (attack.anyAutomatic != 0) {
                    if (attack.anim->endFrame >= 0.0f) {
                        EndAt(chr, attack.anim->endFrame);
                    }
                } else {
                    EndAt(chr, attack.anim->shootEndFrame);
                }
            }
        }
    }

    TickAttackCommon(chr);
}

void ChrActions::TickAttackRoll(Chr& chr) {
    // chrlvTickAttackRoll.
    Chr::ActAttack& attack = chr.actAttack;
    const std::vector<FiringAnim>& rolls = combat_->Rolls();

    if (attack.dualRoll != 0) {
        const float frame = AnimationFrame(chr);
        const int index = ChrCombatTables::IndexIn(rolls, attack.anim);

        if (index >= 4 && index <= 7) {
            if (attack.anim->endFrame <= frame) {
                const bool mirror = Mirrored(chr);
                size_t next = static_cast<size_t>(index) + 4;
                float merge = 16.0f;

                if (attack.fires[1] != 0 && attack.fires[0] != 0) {
                    if ((RandomGetNext() & 1u) == 0) {
                        next += 4;
                    } else {
                        next += 8;
                    }
                }

                // The merge time of each of the animations the roll carries on in.
                static constexpr float kMerges[] = { 24.0f, 24.0f, 32.0f, 44.0f, 24.0f, 34.0f,
                                                     32.0f, 44.0f, 24.0f, 34.0f, 32.0f, 44.0f };
                if (next >= 8 && next <= 19) {
                    merge = kMerges[next - 8];
                }

                const FiringAnim& anim = rolls[next];
                attack.turnState = 2;
                attack.anim = &anim;
                chr.sleep = 0;

                Animate(chr, anim.anim, mirror, anim.startFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), merge);

                if (attack.allSingleShot != 0) {
                    if (anim.recoilEndFrame >= 0.0f) {
                        EndAt(chr, anim.recoilEndFrame);
                    } else {
                        EndAt(chr, anim.shootEndFrame);
                    }
                } else if (anim.recoilStartFrame >= 0.0f) {
                    EndAt(chr, anim.recoilStartFrame);
                } else if (anim.endFrame >= 0.0f) {
                    EndAt(chr, anim.endFrame);
                }

                if (attack.anim->angleOffset != 0.0f) {
                    RootState& root = chr.model->Playback().root;
                    root.headingCorrectionTime = merge;
                    root.headingCorrection = (-attack.anim->angleOffset / merge);

                    if (mirror) {
                        root.headingCorrection = -root.headingCorrection;
                    }
                }
            }
        } else if (index >= 8 && index <= 19 && attack.allSingleShot == 0) {
            if (attack.anim->recoilEndFrame > 0.0f && frame <= attack.anim->recoilEndFrame) {
                if (AnimationLength(chr) <= frame) {
                    RestartAnimation(chr, attack.anim->recoilEndFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);

                    if (attack.anyAutomatic != 0) {
                        if (attack.anim->endFrame >= 0.0f) {
                            EndAt(chr, attack.anim->endFrame);
                        }
                    } else {
                        EndAt(chr, attack.anim->shootEndFrame);
                    }
                }
            }
        }
    }
    TickAttackCommon(chr);
}

void ChrActions::TickAttackWalk(Chr& chr) {
    // chrlvTickAttackWalk.
    Chr::ActAttackWalk& walk = chr.actAttackWalk;
    const ChrTiming& timing = scene_.chrs->Timing();
    const Prop& mine = PropOf(chr);
    const Prop& player = (*scene_.props)[host_.Player().prop];

    walk.walkedTicks += timing.clockTimer;
    chr.lastWalk60 = timing.globalTimer;

    if (chr.invalidMove == 1 || chr.lastMoveOk60 < (timing.globalTimer - kLastMoveOkCheck) ||
        walk.walkLimit < walk.walkedTicks) {
        const AnimationClip* clip = chr.model->Playback().anim;
        const float half = clip != nullptr ? static_cast<float>(clip->frameCount) * 0.5f : 0.0f;
        if (AnimationFrame(chr) > half) {
            chr.model->Playback().RampSpeedToFrame(0.0f, 16.0f);
        } else {
            chr.model->Playback().RampSpeedToFrame(half, 16.0f);
        }

        ai_->SetTargetToPlayer(chr);
        StandUpFacingWalls(chr);

        return;
    }

    const float dx = player.position[0] - mine.position[0];
    const float dz = player.position[2] - mine.position[2];
    if (dx < 300.0f && dx > -300.0f && dz < 300.0f && dz > -300.0f) {
        ai_->SetTargetToPlayer(chr);
        StandUpFacingWalls(chr);

        return;
    }

    if (TurnTowards(chr, player.position, 0, &walk.speed)) {
        walk.moving = 1;
    }

    if (walk.walkedTicks >= kWalkTimerMin) {
        UpdateAimEndSideBack(chr, walk.anim, walk.fires[1] != 0, walk.fires[0] != 0, 1.0f);
    } else {
        ResetAimEnd(chr);
    }

    if (walk.moving != 0 && !(walk.walkedTicks < kWalkTimerMax)) {
        for (int i = 0; i < 2; i++) {
            const size_t hand = static_cast<size_t>(i);
            if (walk.fires[hand] != 0) {
                if (walk.single[hand] == 0) {
                    ToggleFiring(chr, i, true);
                } else if (walk.nextShotTick < walk.walkedTicks &&
                           (i == walk.nextHand || walk.single[static_cast<size_t>(walk.nextHand)] == 0)) {
                    walk.nextShotTick = walk.walkedTicks;

                    if (walk.single[static_cast<size_t>(1 - i)] != 0) {
                        if (walk.launcher[hand] != 0) {
                            walk.nextShotTick += kWalkShotDelayA;
                        } else {
                            walk.nextShotTick += kWalkShotDelayB;
                        }
                    } else if (walk.launcher[hand] != 0) {
                        walk.nextShotTick += kWalkShotDelayC;
                    } else {
                        walk.nextShotTick += kWalkShotDelayD;
                    }

                    walk.nextHand = 1 - walk.nextHand;

                    ToggleFiring(chr, i, true);
                } else {
                    ToggleFiring(chr, i, false);
                }
            } else {
                ToggleFiring(chr, i, false);
            }
        }

        return;
    }

    ToggleFiring(chr, HandLeft, false);
    ToggleFiring(chr, HandRight, false);
}

} // namespace Frigate::Level
