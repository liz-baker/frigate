#include <algorithm>
#include <cmath>
#include <cstring>

#include "level/ChrActions.h"
#include "level/Matrix.h"
#include "level/WeaponTable.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kPi = 3.1415927f;

// TARGET_*: the bits of an attack's target.
constexpr int kTargetBond = 0x01;
constexpr int kTargetFrontOfChr = 0x02;
constexpr int kTargetChr = 0x04;
constexpr int kTargetPad = 0x08;
constexpr int kTargetAimOnly = 0x20;
constexpr int kTargetDontTurn = 0x40;

// CHRLV_SEEN_RECENT_CHECK, CHRLV_RECENT_TIME_CHECK, CHRLV_LASTMOVEOK60_CHECK and
// the attack-walk timings (NTSC).
constexpr int kSeenRecentCheck = 120;
constexpr int kRecentTimeCheck = 181;
constexpr int kLastMoveOkCheck = 60;
constexpr int kWalkTimerMin = 0x15;
constexpr int kWalkTimerMax = 0x1F;
constexpr int kWalkShotDelayA = 0x5A;
constexpr int kWalkShotDelayB = 0x14;
constexpr int kWalkShotDelayC = 0xB4;
constexpr int kWalkShotDelayD = 0x28;

// (float)(u32)randomGetNext() * (1.0f / UINT_MAX).
float RandomUnit() {
    return static_cast<float>(RandomGetNext()) * (1.0f / 4294967296.0f);
}

} // namespace

// -- Starting an attack -----------------------------------------------------------------

void ChrActions::AttackActionRelated(Chr& chr) {
    // chrlvAttackActionRelated.
    const FiringAnim& f = *chr.actAttack.anim;

    if ((chr.actAttack.attackType & kTargetAimOnly) != 0) {
        if (f.recoilStartFrame >= 0.0f && f.recoilStartFrame < f.shootStartFrame) {
            EndAt(chr, f.recoilStartFrame);
        } else {
            EndAt(chr, f.shootStartFrame);
        }
    } else if (chr.actAttack.allSingleShot != 0) {
        if (f.recoilStartFrame >= 0.0f) {
            EndAt(chr, f.recoilStartFrame);
        } else {
            EndAt(chr, f.shootStartFrame);
        }
    } else if (f.recoilStartFrame >= 0.0f) {
        EndAt(chr, f.recoilStartFrame);
    } else if (f.endFrame >= 0.0f) {
        EndAt(chr, f.endFrame);
    } else {
        EndAt(chr, -1.0f);
    }
}

int ChrActions::HeldItem(const Chr& chr, int hand) const {
    // The item of the weapon in a hand (chrGetEquippedWeaponProp's weaponnum);
    // -1 for an empty hand.
    const int prop = Held(chr, hand);
    const PlacedObject* weapon = prop >= 0 ? scene_.objects->ForProp(prop) : nullptr;
    return weapon != nullptr ? weapon->weaponNum : -1;
}

bool ChrActions::SingleShot(int item) const {
    // bondwalkItemGetAutomaticFiringRate(item) < 0.
    const WeaponTable* weapons = host_.Weapons();
    return weapons != nullptr && weapons->Item(item).automaticFiringRate < 0;
}

void ChrActions::InitAttack(Chr& chr, FiringSet set, bool flip, const std::array<int8_t, 2>& hands, int attackType,
                            int entity, int standing) {
    // chrlvInitActAttack.
    const ChrCombatTables& tables = *combat_;
    chr.actionType = ChrAction::Attack;
    int allSingleShot = 1;
    int anyAutomatic = 0;
    std::array<int8_t, 2> single{ 0, 0 };
    std::array<int8_t, 2> launcher{ 0, 0 };

    const float distance = AngleToFaceTarget(chr, attackType, entity);

    int index;
    if (flip) {
        index = static_cast<int>((((kTau - distance) * 32.0f) / kTau) + 0.5f);
    } else {
        index = static_cast<int>(((distance * 32.0f) / kTau) + 0.5f);
    }
    // The game indexes before the table for an angle that is negative, which
    // get_distance_actor_to_position gives for a target more than half a turn
    // behind a character whose heading is past half a turn.
    if (index >= ChrCombatTables::kGroupsPerSet || index < 0) {
        index = 0;
    }

    const std::vector<FiringAnim>& group = tables.Group(set, index);
    const size_t size = group.size();
    size_t next = RandomGetNext() % static_cast<uint32_t>(size);

    const FiringAnim* anim = &group[next];
    if ((chr.chrflags & ChrFlag::ForceNoBlood) != 0 && anim->anim == "fire_hip") {
        anim = &group[(next + 1) % size];
    }

    for (int i = 0; i < 2; i++) {
        if (hands[static_cast<size_t>(i)] != 0) {
            const int item = HeldItem(chr, i);
            if (SingleShot(item)) {
                single[static_cast<size_t>(i)] = 1;
                if (item == kItemLaser) {
                    allSingleShot = 0;
                }
            } else {
                allSingleShot = 0;
                anyAutomatic = 1;
            }

            if (item == kItemRocketLauncher || item == kItemGrenadeLauncher) {
                launcher[static_cast<size_t>(i)] = 1;
            }
        }
    }

    Chr::ActAttack& attack = chr.actAttack;
    attack.turnState = 1;
    attack.anim = anim;
    attack.shotDue = 0;
    attack.nextHand = static_cast<int8_t>(RandomGetNext() & 1u);
    attack.fires = hands;
    attack.single = single;
    attack.launcher = launcher;
    attack.allSingleShot = static_cast<int8_t>(allSingleShot);
    attack.anyAutomatic = static_cast<int8_t>(anyAutomatic);
    attack.slowedTicks = 0;
    attack.shotsFired = 0;

    if (launcher[1] != 0 || launcher[0] != 0) {
        attack.shotsPlanned = (launcher[1] != 0 && launcher[0] != 0) ? 2 : 1;
    } else {
        if ((attackType & 0x80) != 0) {
            attack.shotsPlanned = 1;
        } else {
            attack.shotsPlanned = static_cast<int8_t>((RandomGetNext() & 3u) + 2);
        }

        if (hands[0] != 0 && hands[1] != 0) {
            attack.shotsPlanned = static_cast<int8_t>(attack.shotsPlanned + ((RandomGetNext() & 3u) + 2));
        }
    }

    attack.attackType = attackType;
    attack.entityId = entity;
    attack.standing = standing;
    attack.typeOfMotion = 0;
    attack.lastShotTime = 0;
    attack.attackTime = 0;
    chr.sleep = 0;

    Animate(chr, anim->anim, flip, anim->startFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);

    AttackActionRelated(chr);
}

bool ChrActions::AimAtTarget(Chr& chr, int type, int id) {
    // actor_aim_at_actor.
    if (!ai_->IsNotDeadOrShot(chr) || (host_.HeldGunItem(chr, HandRight) < 0 && host_.HeldGunItem(chr, HandLeft) < 0)) {
        return false;
    }
    // sub_GAME_7F025560.
    AttackFrom(chr, type, id, false);
    return true;
}

bool ChrActions::KneelAimAtTarget(Chr& chr, int type, int id) {
    // actor_kneel_aim_at_actor.
    if (!ai_->IsNotDeadOrShot(chr) || (host_.HeldGunItem(chr, HandRight) < 0 && host_.HeldGunItem(chr, HandLeft) < 0)) {
        return false;
    }
    // sub_GAME_7F0256F0.
    AttackFrom(chr, type, id, true);
    return true;
}

void ChrActions::AttackFrom(Chr& chr, int attackType, int entity, bool kneeling) {
    // sub_GAME_7F025560 (standing) and sub_GAME_7F0256F0 (kneeling).
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);

    std::array<int8_t, 2> hands{ 0, 0 };
    FiringSet set;
    bool flip;

    if (left >= 0 && right >= 0) {
        const bool leftGun = host_.HeldGunItem(chr, HandLeft) >= 0;
        const bool rightGun = host_.HeldGunItem(chr, HandRight) >= 0;

        if (leftGun && rightGun) {
            flip = (RandomGetNext() & 1u) != 0;

            if ((RandomGetNext() % 3u) == 0) {
                set = kneeling ? FiringSet::CrouchedPistol : FiringSet::Pistol;
                hands[HandLeft] = flip;
                hands[HandRight] = !flip;
            } else {
                set = kneeling ? FiringSet::CrouchedDoubles : FiringSet::Doubles;
                hands[HandLeft] = 1;
                hands[HandRight] = 1;
            }
        } else {
            flip = !rightGun;
            set = kneeling ? FiringSet::CrouchedPistol : FiringSet::Pistol;
            hands[HandLeft] = flip;
            hands[HandRight] = !flip;
        }
    } else if (OneHanded(left) || OneHanded(right)) {
        flip = left >= 0;
        set = kneeling ? FiringSet::CrouchedPistol : FiringSet::Pistol;
        hands[HandLeft] = flip;
        hands[HandRight] = !flip;
    } else {
        flip = left >= 0;
        set = kneeling ? FiringSet::CrouchedRifle : FiringSet::Rifle;
        hands[HandLeft] = flip;
        hands[HandRight] = !flip;
    }

    InitAttack(chr, set, flip, hands, attackType, entity, kneeling ? 0 : 1);
}

bool ChrActions::AimAtTargetUpdate(Chr& chr, int type, int id) {
    // actor_fire_or_aim_at_target_update.
    if (chr.actionType == ChrAction::Attack) {
        if ((chr.actAttack.attackType & (kTargetAimOnly | kTargetDontTurn)) != 0) {
            chr.actAttack.attackType = type;
            chr.actAttack.entityId = id;
            AttackActionRelated(chr);
            return true;
        }
    }
    return false;
}

bool ChrActions::FiringWalk(Chr& chr) {
    // actor_walks_and_fires.
    return StartFiringWalk(chr, 0);
}

bool ChrActions::FiringRun(Chr& chr) {
    // actor_runs_and_fires.
    return StartFiringWalk(chr, 1);
}

bool ChrActions::StartFiringWalk(Chr& chr, int speed) {
    if (!ai_->IsNotDeadOrShot(chr)) {
        return false;
    }
    const Prop& mine = PropOf(chr);
    const Prop& bond = (*scene_.props)[host_.Player().prop];
    const ChrTiming& timing = scene_.chrs->Timing();

    if ((host_.HeldGunItem(chr, HandRight) >= 0 || host_.HeldGunItem(chr, HandLeft) >= 0) &&
        (timing.globalTimer - chr.lastWalk60) >= kRecentTimeCheck) {
        const float dx = bond.position[0] - mine.position[0];
        const float dy = bond.position[1] - mine.position[1];
        const float dz = bond.position[2] - mine.position[2];

        if (((dx * dx) + (dy * dy) + (dz * dz)) >= 1000000.0f) {
            InitAttackWalk(chr, speed);
            return true;
        }
    }
    return false;
}

void ChrActions::InitAttackWalk(Chr& chr, int speed) {
    // chrlvInitActAttackWalk.
    const std::vector<FiringAnim>& walks = combat_->Walks();
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);

    std::array<int8_t, 2> hands{ 0, 0 };
    std::array<int8_t, 2> single{ 0, 0 };
    std::array<int8_t, 2> launcher{ 0, 0 };
    const FiringAnim* anim;
    bool flip;

    if (left >= 0 && right >= 0) {
        const bool leftGun = host_.HeldGunItem(chr, HandLeft) >= 0;
        const bool rightGun = host_.HeldGunItem(chr, HandRight) >= 0;

        uint32_t style = 0;
        if (leftGun && rightGun) {
            flip = (RandomGetNext() & 1u) != 0;
            style = RandomGetNext() % 3u;
        } else {
            flip = !rightGun;
        }

        if (style == 0) {
            anim = &walks[speed != 0 ? 3 : 2];

            if (flip) {
                hands[1] = 1;
            } else {
                hands[0] = 1;
            }
        } else if (style == 1) {
            anim = &walks[speed != 0 ? 5 : 4];
            hands[1] = 1;
            hands[0] = 1;
        } else {
            anim = &walks[speed != 0 ? 7 : 6];
            hands[1] = 1;
            hands[0] = 1;
        }
    } else if (OneHanded(left) || OneHanded(right)) {
        flip = left >= 0;
        anim = &walks[speed != 0 ? 3 : 2];
        if (flip) {
            hands[1] = 1;
        } else {
            hands[0] = 1;
        }
    } else {
        flip = left >= 0;
        anim = &walks[speed != 0 ? 1 : 0];
        if (flip) {
            hands[1] = 1;
        } else {
            hands[0] = 1;
        }
    }

    for (int i = 0; i < 2; i++) {
        if (hands[static_cast<size_t>(i)] != 0) {
            const int item = HeldItem(chr, i);
            if (SingleShot(item)) {
                single[static_cast<size_t>(i)] = 1;
            }
            if (item == kItemRocketLauncher || item == kItemGrenadeLauncher) {
                launcher[static_cast<size_t>(i)] = 1;
            }
        }
    }

    chr.actionType = ChrAction::AttackWalk;
    Chr::ActAttackWalk& walk = chr.actAttackWalk;
    walk.walkedTicks = 0;
    walk.walkLimit = static_cast<int32_t>(RandomGetNext() % static_cast<uint32_t>(static_cast<int32_t>(400.0f * host_.AiReactionSpeed()))) +
                     kSeenRecentCheck;
    walk.moving = 0;
    walk.anim = anim;
    walk.nextShotTick = 0;
    walk.nextHand = static_cast<int32_t>(RandomGetNext() & 1u);
    walk.fires = hands;
    walk.single = single;
    walk.launcher = launcher;
    chr.sleep = 0;
    walk.speed = 0.0f;

    Animate(chr, anim->anim, flip, anim->startFrame, 0.5f, 16.0f);
}

bool ChrActions::FiringRoll(Chr& chr) {
    // actor_rolls_fires_crouched.
    if (!ai_->IsNotDeadOrShot(chr)) {
        return false;
    }
    const Prop& mine = PropOf(chr);
    const Prop& bond = (*scene_.props)[host_.Player().prop];

    if (host_.HeldGunItem(chr, HandRight) >= 0 || host_.HeldGunItem(chr, HandLeft) >= 0) {
        const float dx = bond.position[0] - mine.position[0];
        const float dy = bond.position[1] - mine.position[1];
        const float dz = bond.position[2] - mine.position[2];
        const float distance = (dx * dx) + (dy * dy) + (dz * dz);

        // The player is more than 200 away.
        if (200.0f * 200.0f <= distance) {
            const int side = (RandomGetNext() & 1u) == 0;

            if (SideIsClear(chr, side, 200.0f, true)) {
                InitAttackRoll(chr, side);
                return true;
            }

            const int other = side == 0;
            if (SideIsClear(chr, other, 200.0f, true)) {
                InitAttackRoll(chr, other);
                return true;
            }
        }
    }
    return false;
}

void ChrActions::InitAttackRoll(Chr& chr, int side) {
    // chrlvInitActAttackRoll.
    const std::vector<FiringAnim>& rolls = combat_->Rolls();
    const int left = Held(chr, HandLeft);
    const int right = Held(chr, HandRight);

    int dual = 0;
    int anyAutomatic = 0;
    int allSingleShot = 1;
    std::array<int8_t, 2> hands{ 0, 0 };
    std::array<int8_t, 2> single{ 0, 0 };
    std::array<int8_t, 2> launcher{ 0, 0 };
    int flip;

    if (left >= 0 && right >= 0) {
        const bool leftGun = host_.HeldGunItem(chr, HandLeft) >= 0;
        const bool rightGun = host_.HeldGunItem(chr, HandRight) >= 0;

        if (leftGun && rightGun) {
            flip = static_cast<int>(RandomGetNext() & 1u);
            dual = 1;

            if ((RandomGetNext() % 3u) == 0) {
                hands[1] = static_cast<int8_t>(flip);
                hands[0] = flip == 0;
            } else {
                hands[1] = 1;
                hands[0] = 1;
            }
        } else {
            flip = !rightGun;
            dual = 1;
            hands[1] = static_cast<int8_t>(flip);
            hands[0] = flip == 0;
        }
    } else if (OneHanded(left) || OneHanded(right)) {
        flip = left >= 0;
        dual = 1;
        hands[1] = static_cast<int8_t>(flip);
        hands[0] = flip == 0;
    } else {
        flip = left >= 0;
        hands[1] = static_cast<int8_t>(flip);
        hands[0] = flip == 0;
    }

    size_t index;
    if ((side != HandRight && flip == 0) || (side == HandRight && flip != 0)) {
        index = (RandomGetNext() & 1u) != 0 ? 0 : 2;
    } else {
        index = (RandomGetNext() & 1u) != 0 ? 1 : 3;
    }
    if (dual != 0) {
        index += 4;
    }
    const FiringAnim* anim = &rolls[index];

    for (int i = 0; i < 2; i++) {
        if (hands[static_cast<size_t>(i)] != 0) {
            const int item = HeldItem(chr, i);
            if (SingleShot(item)) {
                single[static_cast<size_t>(i)] = 1;
                if (item == kItemLaser) {
                    allSingleShot = 0;
                }
            } else {
                allSingleShot = 0;
                anyAutomatic = 1;
            }

            if (item == kItemRocketLauncher || item == kItemGrenadeLauncher) {
                launcher[static_cast<size_t>(i)] = 1;
            }
        }
    }

    chr.actionType = ChrAction::AttackRoll;
    Chr::ActAttack& attack = chr.actAttack;
    attack.anim = anim;
    attack.shotDue = 0;
    attack.nextHand = static_cast<int8_t>(RandomGetNext() & 1u);
    attack.fires = hands;
    attack.single = single;
    attack.launcher = launcher;
    attack.allSingleShot = static_cast<int8_t>(allSingleShot);
    attack.anyAutomatic = static_cast<int8_t>(anyAutomatic);
    attack.dualRoll = static_cast<int8_t>(dual);
    attack.slowedTicks = 0;
    attack.shotsFired = 0;
    attack.turnState = 1;

    if (launcher[1] != 0 || launcher[0] != 0) {
        attack.shotsPlanned = (launcher[1] != 0 && launcher[0] != 0) ? 2 : 1;
    } else {
        attack.shotsPlanned = static_cast<int8_t>((RandomGetNext() & 3u) + 2);

        if (hands[0] != 0 && hands[1] != 0) {
            attack.shotsPlanned = static_cast<int8_t>(attack.shotsPlanned + ((RandomGetNext() & 3u) + 2));
        }
    }

    // The union holds the roll's own state where an attack keeps its target.
    attack.attackType = 1;
    attack.entityId = 0;
    attack.standing = 1;
    attack.typeOfMotion = 0;
    attack.lastShotTime = 0;
    attack.attackTime = 0;
    chr.sleep = 0;

    Animate(chr, anim->anim, flip != 0, anim->startFrame, ai_->SpeedRating(chr, 0.5f, 0.8f), 16.0f);

    if (dual == 0) {
        if (allSingleShot != 0) {
            if (anim->recoilEndFrame >= 0.0f) {
                EndAt(chr, anim->recoilEndFrame);
            } else {
                EndAt(chr, anim->shootEndFrame);
            }
        } else if (anim->recoilStartFrame >= 0.0f) {
            EndAt(chr, anim->recoilStartFrame);
        } else if (anim->endFrame >= 0.0f) {
            EndAt(chr, anim->endFrame);
        }
    }
}

// -- Aiming ---------------------------------------------------------------------------------

float ChrActions::SubrotySideback(const Chr& chr) const {
    // chrlvGetSubrotySideback.
    float result = ChrHeading(chr) + chr.aimSideBack;
    float offset = 0.0f;

    if (result >= kTau) {
        result = result - kTau;
    } else if (result < 0.0f) {
        result = result + kTau;
    }

    if (chr.actionType == ChrAction::Attack || chr.actionType == ChrAction::AttackRoll) {
        offset = chr.actAttack.anim->angleOffset;
    }

    if (offset != 0.0f) {
        if (Mirrored(chr)) {
            offset = kTau - offset;
        }
        result = result + offset;

        if (result >= kTau) {
            result = result - kTau;
        }
    }
    return result;
}

float ChrActions::AimLimitAngle(float squaredDistance) {
    // chrlvGetAimLimitAngle.
    if (squaredDistance > (1600.0f * 1600.0f)) {
        return kPi / 167.5f;
    }
    if (squaredDistance > (800.0f * 800.0f)) {
        return kPi / 83.5f;
    }
    if (squaredDistance > (400.0f * 400.0f)) {
        return kPi / 42.0f;
    }
    if (squaredDistance > (200.0f * 200.0f)) {
        return kPi / 21.0f;
    }
    return kPi / 12.5f;
}

float ChrActions::AimUpBackShoulder(const Chr& chr) {
    // sub_GAME_7F02C27C.
    float result = chr.aimUpRightShoulder + chr.aimUpBack;
    if (result < 0.0f) {
        result = result + kTau;
    }
    return result;
}

void ChrActions::UpdateAimEndShoulders(Chr& chr, const FiringAnim* anim, bool same, bool swap, float next) {
    // chrlvUpdateAimendbackShoulders.
    float nextRight = 0.0f;
    float nextBack = 0.0f;
    float nextLeft = next;

    if (anim != nullptr) {
        if (anim->maxUp < next) {
            nextBack = next - anim->maxUp;
            nextLeft = anim->maxUp;
        } else if (next < anim->maxDown) {
            nextBack = next - anim->maxDown;
            nextLeft = anim->maxDown;
        }

        if (nextLeft > 0.0f) {
            nextRight = anim->freeArmFracUp * nextLeft;
        } else {
            nextRight = anim->freeArmFracDown * nextLeft;
        }
    }

    if (swap) {
        chr.aimEndRightShoulder = nextLeft;

        if (same) {
            chr.aimEndLeftShoulder = nextLeft;
        } else {
            chr.aimEndLeftShoulder = nextRight;
        }
    } else {
        chr.aimEndRightShoulder = nextRight;
        chr.aimEndLeftShoulder = nextLeft;
    }

    chr.aimEndBack = nextBack;
}

bool ChrActions::MuzzleMatrixPosition(Chr& chr, int hand, const Mtxf& viewToWorld, Vec3* out) {
    // The common part of sub_GAME_7F02D630 and chrlvUpdateAimendsideback's
    // muzzle: the position of the gunfire node (or, without one, the origin
    // of the node in slot 1) of the weapon in `hand`, in the world. False when
    // the weapon was not drawn or has neither.
    PlacedObject* weapon = HeldObject(chr, hand);
    if (weapon == nullptr || !weapon->instance) {
        return false;
    }
    if ((scene_.objects->Props()[Held(chr, hand)].flags & PropFlag::OnScreen) == 0) {
        return false;
    }

    std::array<float, 3> origin{};
    std::array<float, 16> raw{};
    if (weapon->instance->SwitchOrigin(0, &origin)) {
        if (!weapon->instance->SwitchMatrix(0, &raw)) {
            return false;
        }
        Mtxf nodeMatrix;
        std::memcpy(nodeMatrix.m, raw.data(), sizeof(nodeMatrix.m));
        const Mtxf world = MultiplyHomogeneous(viewToWorld, nodeMatrix);
        *out = TransformVector(world, { origin[0], origin[1], origin[2] });
        return true;
    }
    if (weapon->instance->SwitchOrigin(1, &origin)) {
        if (!weapon->instance->SwitchMatrix(1, &raw)) {
            return false;
        }
        Mtxf nodeMatrix;
        std::memcpy(nodeMatrix.m, raw.data(), sizeof(nodeMatrix.m));
        const Mtxf world = MultiplyHomogeneous(viewToWorld, nodeMatrix);
        *out = { world.m[3][0], world.m[3][1], world.m[3][2] };
        return true;
    }
    return false;
}

bool ChrActions::UpdateAimEndSideBack(Chr& chr, const FiringAnim* anim, bool same, bool swap, float spread) {
    // chrlvUpdateAimendsideback.
    bool result = true;
    float elevation = 0.0f;
    int attackType = kTargetBond;
    int entity = 0;
    float sideBack = 0.0f;

    if (chr.actionType == ChrAction::Attack) {
        attackType = chr.actAttack.attackType;
        entity = chr.actAttack.entityId;
    } else if (chr.actionType == ChrAction::Stand) {
        attackType = chr.actStand.faceEntityType;
        entity = chr.actStand.faceEntityId;
    }

    if ((attackType & kTargetFrontOfChr) == 0) {
        const Prop& player = (*scene_.props)[host_.Player().prop];
        const Prop& mine = PropOf(chr);
        const Vec3 playerPosition = player.position;
        // current_player_pos: the player, or what the attack names instead.
        Vec3 targetPosition = playerPosition;

        float dx = playerPosition[0] - mine.position[0];
        float dy = playerPosition[1] - mine.position[1];
        float dz = playerPosition[2] - mine.position[2];

        const float squared = (dx * dx) + (dy * dy) + (dz * dz);

        bool seen;
        if ((attackType & kTargetBond) != 0) {
            if ((attackType & kTargetDontTurn) != 0) {
                seen = true;
            } else {
                seen = ai_->CanSeeBond(chr);
            }
        } else {
            seen = true;
        }

        if ((attackType & kTargetBond) != 0) {
            const float ducking = host_.PlayerDuckingHeight();
            if ((chr.chrflags & ChrFlag::ForceNoBlood) != 0) {
                if (((dx * dx) + (dy * dy) + (dz * dz)) < 160000.0f) {
                    if (mine.position[1] < (playerPosition[1] - (2.0f * ducking))) {
                        dy -= ducking * (0.55f + (0.1f * RandomUnit() * spread));
                    } else if ((playerPosition[1] - (ducking * 0.5f)) < mine.position[1]) {
                        dy -= ducking * (0.15f + (0.1f * RandomUnit() * spread));
                    } else {
                        dy = ((RandomUnit() * 0.1f * spread) + 1.0f) * 40.0f;
                    }
                } else {
                    dy += ducking * (0.025f - (0.05f * RandomUnit() * spread));
                }
            } else if (((dx * dx) + (dy * dy) + (dz * dz)) > 1000000.0f) {
                if ((RandomGetNext() % 3u) == 0) {
                    dy += ducking * (0.05f + (0.1f * RandomUnit() * spread));
                } else {
                    dy -= ducking * (0.05f + (0.55f * RandomUnit() * spread));
                }
            } else {
                if (mine.position[1] < (playerPosition[1] - ducking)) {
                    dy -= ducking * (0.55f + (0.1f * RandomUnit() * spread));
                } else if ((playerPosition[1] - (ducking * 0.5f)) < mine.position[1]) {
                    dy -= ducking * (0.15f + (0.1f * RandomUnit() * spread));
                } else {
                    dy = ((RandomUnit() * 0.1f * spread) - 0.05f) * ducking;
                }
            }
        } else {
            const Frigate::Vec3& origin = chr.model->Playback().root.pos;
            int tile = kNoTile;
            targetPosition = TargetLocation(chr, attackType, entity, &tile);
            dx = targetPosition[0] - origin.x;
            dy = targetPosition[1] - origin.y;
            dz = targetPosition[2] - origin.z;
        }

        if ((attackType & 0x100) == 0) {
            const float horizontal = std::sqrt((dx * dx) + (dz * dz));
            elevation = std::atan2(dy, horizontal);

            if (elevation >= kPi) {
                elevation = elevation - kTau;
            }
        }

        if (seen) {
            const float subroty = SubrotySideback(chr);
            const int weaponHand = swap ? HandRight : HandLeft;
            bool haveMuzzle = false;
            Vec3 muzzle{};

            // This is a slight variation of sub_GAME_7F02D630: the matrices are
            // those of the last frame, which the previous view-to-world turns
            // into the world.
            if (HeldObject(chr, weaponHand) != nullptr && squared < 1000000.0f) {
                haveMuzzle = MuzzleMatrixPosition(chr, weaponHand, host_.PreviousViewToWorld(), &muzzle);
            }

            if (haveMuzzle) {
                const Vec3 direction = { std::sin(subroty), 0.0f, std::cos(subroty) };
                const Vec3 side = { mine.position[0] - dz, mine.position[1], mine.position[2] + dx };
                const Vec3 meet = LineLineIntersection(mine.position, side, muzzle, direction);
                dx = targetPosition[0] - meet[0];
                dz = targetPosition[2] - meet[2];
            }

            float aim = std::atan2(dx, dz);

            sideBack = aim - subroty;
            if (aim < subroty) {
                sideBack = aim - subroty + kTau;
            }

            const RootState& root = chr.model->Playback().root;
            if (root.headingCorrectionTime > 0.0f) {
                sideBack = sideBack - (root.headingCorrectionTime * root.headingCorrection);

                if (sideBack < 0.0f) {
                    sideBack = sideBack + kTau;
                }

                if (sideBack >= kTau) {
                    sideBack = sideBack - kTau;
                }
            }

            if ((attackType & 1) != 0 && (attackType & 0x60) == 0) {
                const ChrTiming& timing = scene_.chrs->Timing();
                aim = (static_cast<float>(
                           (static_cast<int32_t>(static_cast<float>(timing.globalTimer) * chr.model->Playback().playSpeed) +
                            chr.chrnum) %
                           60) *
                       kTau) /
                      60.0f;
                aim = std::sin(aim) * (AimLimitAngle(squared) * 0.5f);
                sideBack += aim;

                if (sideBack < 0.0f) {
                    sideBack = sideBack + kTau;
                }

                if (sideBack >= kTau) {
                    sideBack = sideBack - kTau;
                }
            }

            if (sideBack >= kPi) {
                sideBack = sideBack - kTau;
            }

            sideBack += chr.aimSideBack;

            if (Mirrored(chr)) {
                if (sideBack < -anim->maxLeft) {
                    sideBack = -anim->maxLeft;
                    result = false;
                } else if (-anim->maxRight < sideBack) {
                    sideBack = -anim->maxRight;
                    result = false;
                }
            } else {
                if (anim->maxLeft < sideBack) {
                    sideBack = anim->maxLeft;
                    result = false;
                } else if (sideBack < anim->maxRight) {
                    sideBack = anim->maxRight;
                    result = false;
                }
            }
        }
    }

    UpdateAimEndShoulders(chr, anim, same, swap, elevation);
    chr.aimEndSideBack = sideBack;
    chr.aimEndCount = 0xA;

    return result;
}

} // namespace Frigate::Level
