#pragma once

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
    ChrActions(const AiScene& scene, AiHost& host, ModelFactory* models, std::optional<AnimationTable> table)
        : scene_(scene), host_(host), models_(models), table_(std::move(table)) {
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
    bool Sidestep(Chr& chr) override;
    bool SideHop(Chr& chr) override;
    bool SideRun(Chr& chr) override;
    bool FaceTarget(Chr& chr, int type, int id) override;
    bool PlotCourse(Chr& chr, const Vec3& position, int tile, Speed speed) override;
    void StartPatrol(Chr& chr, int path) override;
    void StartIdleAnimation(Chr& chr, float duration, bool unarmed, bool mirror) override;

    // chrlvIsArrivingLaterallyAtPos and chrlvGeometryRelated7F02FC34.
    static bool ArrivingAt(const Vec3& previous, const Vec3& current, const Vec3& target, float range);
    static bool WithinRangeAhead(const Vec3& origin, const Vec3& direction, const Vec3& target, float range);
    // chrlvLineLineIntersection.
    static Vec3 LineLineIntersection(const Vec3& line1A, const Vec3& line1B, const Vec3& point, const Vec3& direction);

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

    // get_distance_actor_to_position: the angle from the character's heading
    // round to `position`.
    float AngleTo(const Chr& chr, const Vec3& position) const;
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

    AiScene scene_;
    AiHost& host_;
    ModelFactory* models_;
    std::optional<AnimationTable> table_;
    ChrAi* ai_ = nullptr;
    std::unordered_map<std::string, const AnimationClip*> clips_;
    std::optional<Strides> strides_;
};

} // namespace Frigate::Level
