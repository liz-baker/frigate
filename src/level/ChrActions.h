#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "level/AnimationTable.h"
#include "level/ChrAi.h"

namespace Frigate::Level {

// The non-combat half of chraction.c: the animations a character starts, and
// the idle, animation, route-following and run-to-position actions it ticks
// (ChrAi runs the AI list that chooses among them).
class ChrActions final : public AiActions {
  public:
    // `models` supplies the animation clips and `table` names them; without
    // either the characters start no animations.
    ChrActions(const AiScene& scene, AiHost& host, ModelFactory* models, std::optional<AnimationTable> table,
               std::shared_ptr<const ChrCombatTables> combat = nullptr)
        : scene_(scene), host_(host), models_(models), table_(std::move(table)), combat_(std::move(combat)) {
    }

    // The interpreter whose queries the actions use; set once it exists.
    void Attach(ChrAi* ai) {
        ai_ = ai;
    }

    void Tick(Chr& chr) override;

    void Stop(Chr& chr) override;
    void Kneel(Chr& chr) override;
    void PointAtBond(Chr& chr) override;
    void LookSurprised(Chr& chr) override;
    void RaiseArms(Chr& chr) override;
    void FadeOut(Chr& chr) override;
    void StartAlarm(Chr& chr) override;
    void PlayAnimation(Chr& chr, int animation, int startFrame, int endFrame, uint8_t bitfield,
                       int interpolationTicks) override;
    void PlayAircraftAnimation(PlacedObject& aircraft, int animation, int startFrame, int endFrame,
                               int interpolationTicks) override;
    // Whether `clip` is entry `index` of the aircraft's animation table
    // (animation_table_ptrs2).
    bool IsAircraftClip(const AnimationClip* clip, int index);
    bool Sidestep(Chr& chr) override;
    bool SideHop(Chr& chr) override;
    bool SideRun(Chr& chr) override;
    bool FaceTarget(Chr& chr, int type, int id) override;
    bool PlotCourse(Chr& chr, const Vec3& position, int tile, Speed speed) override;
    void StartPatrol(Chr& chr, int path) override;
    void StartIdleAnimation(Chr& chr, float duration, bool unarmed, bool mirror) override;
    void Surrender(Chr& chr) override;
    bool FiringWalk(Chr& chr) override;
    bool FiringRun(Chr& chr) override;
    bool FiringRoll(Chr& chr) override;
    bool AimAtTarget(Chr& chr, int type, int id) override;
    bool KneelAimAtTarget(Chr& chr, int type, int id) override;
    bool AimAtTargetUpdate(Chr& chr, int type, int id) override;
    bool ThrowGrenade(Chr& chr) override;
    bool HitChr(Chr& target, int part, const Vec3& direction, int item) override;
    bool HandleShot(Chr& target, int part, const Vec3& direction, int item, bool fromPlayer) override;
    bool ExplosionDamage(Chr& target, const Vec3& position, float damage, bool sound) override;
    void FlinchRandomly(Chr& chr) override;
    void UpdateAimProperties(Chr& chr) override;
    void TriggerFireWeapon(Chr& chr) override;
    void DropItems(Chr& chr) override;
    void StopFiring(Chr& chr) override;
    // chrSetFiring(chr, hand, FALSE).
    void StopFiringHand(Chr& chr, int hand) {
        SetFiring(chr, hand, false);
    }

    // What playerTick works from for a player whose character the current
    // player sees (the player's own fields), and keeps from tick to tick.
    struct PlayerBody {
        bool dead = false;
        // playerGetCrouchPos: 0 squatting, 1 half, 2 standing.
        int crouch = 2;
        float speedSideways = 0.0f;
        float speedForwards = 0.0f;
        float speedTheta = 0.0f;
        int headAnimation = -1;
        // vv_theta, in degrees.
        float theta = 0.0f;
        // field_2A08 and field_2A0C: the pitch and heading the sight points at.
        float aimPitch = 0.0f;
        float aimYaw = 0.0f;
        // hands[].field_87D: the hand fires.
        std::array<bool, 2> firing{};
        // g_bondviewBondDeathAnimations.
        const std::vector<const AnimationClip*>* deathClips = nullptr;
        // collision_position and current_tile_ptr.
        Vec3 position{};
        int tile = kNoTile;
        // players_cur_animation, field_1288 and field_1280.
        const AnimationClip* clip = nullptr;
        float clipSpeed = 0.0f;
        float lean = 0.0f;
    };
    // The part of playerTick ahead of the chrTick that poses the character:
    // `full` is g_PlayerTickCount >= 2, which picks its animation.
    void PosePlayerBody(Chr& chr, PlayerBody& body, bool full);
    // sub_GAME_7F02D630: the world position of the muzzle of the weapon in
    // `hand`, false when the weapon was not drawn this frame.
    bool GunMuzzle(Chr& chr, int hand, const Mtxf& viewToWorld, Vec3* out);

    // The body of Bond in the swirl starts animation `index` of
    // stage_intro_anim_table and ACT_BONDINTRO (bondviewSetCameraMode's
    // CAMERAMODE_SWIRL entry).
    void StartBondIntro(Chr& chr, int index);

    // chrlvGetSubrotySideback: the heading the character's torso points at,
    // which the joint poses of ChrDraw turn the back by.
    float SubrotySideback(const Chr& chr) const;
    // chrGetFlinchAmount.
    static float FlinchAmount(const Chr& chr);
    // chrHandleJointPositioned: the head, torso and arms of a character turn to
    // where it aims and flinch from a hit. `bodypart` is the index of the
    // joint's first matrix.
    void PositionJoint(Chr& chr, int bodypart, Mat4* matrix, const Mtxf& viewToWorld, const Mtxf& worldToScreen);

    // chrlvIsArrivingLaterallyAtPos and chrlvGeometryRelated7F02FC34.
    static bool ArrivingAt(const Vec3& previous, const Vec3& current, const Vec3& target, float range);
    static bool WithinRangeAhead(const Vec3& origin, const Vec3& direction, const Vec3& target, float range);
    // get_distance_actor_to_position: the angle from the character's heading
    // round to `position`.
    float AngleTo(const Chr& chr, const Vec3& position) const;
    // chrlvGetPatrolPercentOrPosition: where a character is, counting how far a
    // character no one sees has got along its patrol or route.
    Vec3 PatrolPercentOrPosition(const Chr& chr) const;
    // chrlvLineLineIntersection.
    static Vec3 LineLineIntersection(const Vec3& line1A, const Vec3& line1B, const Vec3& point, const Vec3& direction);
    // chrlvGetAimLimitAngle.
    static float AimLimitAngle(float squaredDistance);
    // chrlvStanLineDirIntersection.
    Vec3 EdgeIntersection(const Vec3& point, const Vec3& direction) const;

  private:
    // A point and the tile it is on.
    struct Step {
        Vec3 position{};
        int tile = kNoTile;
    };

    // How far a clip carries a character each tick (initWeaponAnimGroups'
    // D_80030984 and the eight after it), found from the clips' root motion.
    struct Strides {
        float walk = 0.0f;
        float run = 0.0f;
        float sprint = 0.0f;
        float walkUnarmed = 0.0f;
        float runOneHanded = 0.0f;
        float sprintOneHanded = 0.0f;
        float walkFemale = 0.0f;
        float runFemale = 0.0f;
        float sprintOneHandedAgain = 0.0f;
    };

    // -- Animations (ChrActions.cpp) ---------------------------------------------

    // The clip named `name`, loaded once; null (after the factory logs why)
    // when there is none.
    const AnimationClip* Clip(const std::string& name);
    // The clip of characters' animation `id`.
    const AnimationClip* ClipForId(int id);
    // modelSetAnimation with a clip; false when the character has no model or
    // the clip does not load.
    bool Animate(Chr& chr, const std::string& name, bool flip, float startFrame, float speed, float merge);
    bool AnimateId(Chr& chr, int id, bool flip, float startFrame, float speed, float merge);
    // Whether the character's model plays the clip named `name`.
    bool IsPlaying(const Chr& chr, const std::string& name);
    // modelGetAnimFrame and modelGetAnimEndFrame, and whether the first has
    // reached the second.
    static float AnimationFrame(const Chr& chr);
    static float AnimationLength(const Chr& chr);
    static bool AnimationEnded(const Chr& chr);
    // The current clip's frame count less one.
    static float LastFrame(const Chr& chr);
    // modelSetAnimEndFrame, modelSetAnimLooping and the model's gunhand (mirror).
    static void EndAt(Chr& chr, float frame);
    static void Loop(Chr& chr, float loopFrame, float merge);
    static bool Mirrored(const Chr& chr);

    float ClipStride(Chr& chr, const char* name, bool multiplyFirst);
    const Strides& StridesFor(Chr& chr);
    // chrlvModelScaleAnimationRelated: the distance a tick of the character's
    // current walking or running animation carries it.
    float AnimationStride(Chr& chr);

    // -- Held weapons ---------------------------------------------------------------

    // chrGetEquippedWeaponProp.
    static int Held(const Chr& chr, int hand) {
        return chr.weaponsHeld[static_cast<size_t>(hand)];
    }
    // weaponIsOneHanded for the weapon in prop `prop`.
    bool OneHanded(int prop) const;
    // Both hands or neither hold a weapon, or one holds a one-handed weapon:
    // the case most animations pick the unarmed variant for.
    bool EitherHandFree(const Chr& chr) const;

    // -- Standing, kneeling and scripted animations ----------------------------------------

    // chrlvKneelingAnimationRelated, and the variants of it and of
    // chrlvMergeKneelToStand that go on to check for a wall in front.
    void StandUp(Chr& chr);
    void StandUpFacingWalls(Chr& chr);
    void MergeToStandFacingWalls(Chr& chr, float mergeTime);
    // chrlvSpotBondAnimationRelated.
    void StartSpotBond(Chr& chr, float interpolation);
    // chrlvSideStepAnimationRelated and chrlvFireJumpToSideAnimationRelated.
    void StartSidestep(Chr& chr, int side);
    void StartSideHop(Chr& chr, int side);

    // -- Facing ----------------------------------------------------------------------------

    // chrlvGetChrOrPresetLocation: flag 4 looks up a character, 8 a pad, and
    // anything else is the player.
    Vec3 TargetLocation(Chr& chr, int flags, int id, int* tile);
    // chrlvDistanceToChrRelated.
    float AngleToFaceTarget(Chr& chr, int type, int id);
    // chrlvSetSubroty: turns a standing character towards what it faces.
    int TurnToFace(Chr& chr, int state, float lastFrame, float rate, float offset);
    // chrlvPathingCollisionRelated: how far the character could walk along
    // `angle`.
    float ClearDistance(Chr& chr, float angle, float distance, int cdTypes, float height, float above);

    // -- The ticks of the actions ---------------------------------------------------------------

    void TickStand(Chr& chr);
    void TickAnim(Chr& chr);
    void TickDead(Chr& chr);
    void TickSurprised(Chr& chr);
    void TickRunPos(Chr& chr);
    void TickPatrol(Chr& chr);
    void TickGoPos(Chr& chr);

    // -- Sidestepping ------------------------------------------------------------------------------

    // chrlvCall7F0B0E24WithChrWidthHeight: whether the character, a width to
    // either side of the line from it to `to`, can walk it.
    bool SidestepClear(Chr& chr, const Vec3& to, const Vec3& direction);
    // sub_GAME_7F02A1E8 (`towardsPlayer` false: the character's own side) and
    // sub_GAME_7F02A0EC (a side of the line to the player).
    bool SideIsClear(Chr& chr, int side, float distance, bool towardsPlayer);

    // -- Moving (ChrMovement.cpp) ----------------------------------------------------------------------

    Prop& PropOf(const Chr& chr) const;
    // play_sound_for_shot_actor: the yelp of a character that is hit.
    void PlayShotSound(const Chr& chr);
    // sub_GAME_7F02BFE4: the gunshot of the character's `hand`, when `shooting`
    // (it fired or is between shots).
    void PlayFireSound(Chr& chr, int hand, int item, bool shooting);
    // The character's slot in the stage's characters.
    int SlotOf(const Chr& chr) const;
    // The animation a character starts moving with at SPEED `speed`
    // (get_sound_at_range), and the walking animation of a patrol
    // (chrlvWalkingAnimationRelated).
    void StartMovingAnimation(Chr& chr, int speed);
    void StartWalkingAnimation(Chr& chr);
    // sub_GAME_7F024CF8.
    void StartRunToPosition(Chr& chr, const Vec3& position);
    // chrlvApplySpeed: turns the character towards `target` at the SPEED
    // `mode`, true once it faces it.
    bool TurnTowards(Chr& chr, const Vec3& target, int mode, float* speed);

    // chrlvActGoposRelated.
    Step GoPosTarget(const Chr& chr) const;
    // chrlvSetGoposSegDistTotal: starts the "magic" way of moving, straight
    // along the route, for a character no one sees.
    void BeginMagic(Chr& chr, Chr::WayData* way, const Vec3& target);
    // chrlvTravelTickMagic.
    void TickMagic(Chr& chr, Chr::WayData* way, float stride, const Vec3& target, int targetTile);
    // chrlvTravelTick.
    void TravelTick(Chr& chr, const Vec3& target, int targetTile, Chr::WayData* way);
    // chrlvStanRoomRelated: whether the line to `position` crosses no room that is drawn.
    bool OffScreenRoute(Chr& chr, const Vec3& position, int tile);
    // chrlvIsPosClearOfObjectBounds.
    bool ClearOfObjects(const Vec3& position, int tile);

    // chrlvPatrolCalculateStep, chrlvGetPatrolStepPad and the steps that move
    // along the path.
    int PatrolCalculateStep(const Chr& chr, bool* forward, int steps) const;
    const Pad& PatrolPad(const Chr& chr, int steps) const;
    void SetPatrolTarget(Chr& chr);
    void AdvancePatrolStep(Chr& chr);
    // chrlvActGoposSetTargetPosRelated, chrlvActGoposIncCurIndex and
    // chrlvPlotCourseRelated.
    void SetGoPosTarget(Chr& chr);
    void AdvanceGoPos(Chr& chr);
    void ConsiderReplotting(Chr& chr);
    // chrlvMovementTargetRelated.
    int TicksToTarget(Chr& chr);

    // The tests of a step along a route: sub_GAME_7F030128, sub_GAME_7F0301FC
    // and sub_GAME_7F0304AC.
    bool LineClear(Chr& chr, const Vec3& from, int fromTile, const Vec3& to, int toTile, int cdTypes);
    bool SidesClear(Chr& chr, const Vec3& from, int fromTile, const Vec3& to, float reach, int cdTypes);
    bool LineViaPoint(Chr& chr, const Vec3& from, int fromTile, const Vec3& via, const Vec3& to, int toTile,
                      int cdTypes);
    // chrlvSwapIfDiffArg2Determinate.
    static void OrderEdge(Vec3* a, Vec3* b, const Vec3& direction);
    // sub_GAME_7F03081C and (with `nearest`) sub_GAME_7F030D70: true when the
    // way to `dest` is clear, otherwise the ends of the obstacle in it.
    bool FindEdges(Chr& chr, const Vec3& from, int fromTile, const Vec3& dest, Vec3* edgeA, Vec3* edgeB, float extend,
                   float radius, int cdTypes, bool nearest);
    // sub_GAME_7F03130C.
    bool FindDetour(Chr& chr, Vec3* base, bool baseIsFirst, Vec3* other, float reach, bool checkTarget,
                    const Vec3* target, Chr::WayData* way, float margin, int cdTypes, bool setCopy);

    // -- Combat (ChrCombat.cpp, ChrAttack.cpp and ChrFiring.cpp) ----------------------------------------------

    // The switch-array slots of a weapon's model that chrSetFiring shows: its
    // gunfire node and the muzzle flash's switch.
    static constexpr size_t kGunfireSlot = 0;
    static constexpr size_t kWeaponFlashSlot = 2;
    // CHRLV_DEFAULT_TIMER.
    static constexpr int kDefaultTimer = 180;

    // The object of the weapon in a hand, or null.
    PlacedObject* HeldObject(const Chr& chr, int hand) const;
    // The item of the weapon in a hand, -1 for an empty hand.
    int HeldItem(const Chr& chr, int hand) const;
    // bondwalkItemGetAutomaticFiringRate(item) < 0: the weapon fires single shots.
    bool SingleShot(int item) const;
    // chrSetFiring, chrlvResetAimend and chrlvToggleHiddenRelated.
    void SetFiring(Chr& chr, int hand, bool firing);
    void ResetAimEnd(Chr& chr);
    void ToggleFiring(Chr& chr, int hand, bool fire);
    // chrlvMaybeSameRoom.
    bool MaybeSameRoom(Chr& chr, const Vec3& position, int tile);
    // chrlvCall7F02982C: whether the character could walk `distance` along `direction`.
    bool CircleClear(Chr& chr, const Vec3& direction, float distance);
    // chrlvPathingCollisionRelated7F0264B0.
    float ClearBehind(Chr& chr, float angle, float distance);
    // chrlvGetGuard007ArghRating.
    float ArghRating(const Chr& chr, float min, float max) const;
    // modelSetAnimationWithMerge.
    bool AnimateMerge(Chr& chr, const std::string& name, bool flip, float startFrame, float speed, float merge,
                      bool doMerge);
    // modelSetAnimation with the clip that is playing.
    void RestartAnimation(Chr& chr, float startFrame, float speed, float merge);

    // chrlvAttackAnimationRelated7F026F30: whether a character in a roll holds
    // its reaction to a shot until the roll's frame `*endFrame`.
    bool HoldsForShot(Chr& chr, float* endFrame);
    // triggered_on_shot_hit: the reaction of a character to a hit on body part
    // `requestedAnim`, along `direction`, from `angle`.
    void ShotReaction(Chr& chr, const Vec3& direction, float angle, int requestedAnim, int item);
    // chrlvIterateGuardSeeShotDie.
    void IterateSeeShotDie(Chr& chr, bool died);
    // chrlvThrowGrenade.
    void StartThrowGrenade(Chr& chr, int hand, bool fromStart);

    void TickDie(Chr& chr);
    void TickArgh(Chr& chr);
    void TickPreArgh(Chr& chr);
    void TickSurrender(Chr& chr);
    void TickThrowGrenade(Chr& chr);
    void TickBondIntro(Chr& chr);
    void TickAttack(Chr& chr);
    void TickAttackCommon(Chr& chr);
    void TickAttackRoll(Chr& chr);
    void TickAttackWalk(Chr& chr);

    // chrlvAttackActionRelated.
    void AttackActionRelated(Chr& chr);
    // chrlvInitActAttack through sub_GAME_7F025560 and sub_GAME_7F0256F0: the
    // attack with the weapons held, standing or kneeling.
    void AttackFrom(Chr& chr, int attackType, int entity, bool kneeling);
    void InitAttack(Chr& chr, FiringSet set, bool flip, const std::array<int8_t, 2>& hands, int attackType, int entity,
                    int standing);
    // chrlvInitActAttackWalk and its two callers.
    bool StartFiringWalk(Chr& chr, int speed);
    void InitAttackWalk(Chr& chr, int speed);
    // chrlvInitActAttackRoll.
    void InitAttackRoll(Chr& chr, int side);
    // chrlvAttackrollAnimationRelated7F02E2E0 and ...7F02E3B8.
    bool FinishRollAnimation(Chr& chr);
    void RecoverFromRoll(Chr& chr);

    // sub_GAME_7F02C27C and chrlvUpdateAimendbackShoulders.
    static float AimUpBackShoulder(const Chr& chr);
    static void UpdateAimEndShoulders(Chr& chr, const FiringAnim* anim, bool same, bool swap, float next);
    // chrlvUpdateAimendsideback: false when the aim is held back by `anim`'s limits.
    bool UpdateAimEndSideBack(Chr& chr, const FiringAnim* anim, bool same, bool swap, float spread);
    // The world position of the gunfire node (or the first node after it) of the
    // weapon in `hand`, from the matrices it was last posed with: sub_GAME_7F02D630.
    bool MuzzleMatrixPosition(Chr& chr, int hand, const Mtxf& viewToWorld, Vec3* out);
    // chrlvUpdateShotbondsum.
    void UpdateShotBondSum(Chr& chr, bool* aimedAt, bool* hurt, int item);
    // chrlvAttackRelated7F0292A8: whether a shot from `from` can reach what the
    // character attacks.
    bool ShotReachesTarget(Chr& chr, const Vec3& from, int fromTile);
    // chrlvFireWeaponRelated.
    void FireWeapon(Chr& chr, int hand);

    AiScene scene_;
    AiHost& host_;
    ModelFactory* models_;
    std::optional<AnimationTable> table_;
    std::shared_ptr<const ChrCombatTables> combat_;
    ChrAi* ai_ = nullptr;
    std::unordered_map<std::string, const AnimationClip*> clips_;
    std::optional<Strides> strides_;
};

} // namespace Frigate::Level
