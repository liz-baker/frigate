#pragma once

#include <cstdint>
#include <string>

#include "level/AiLists.h"
#include "level/Chrs.h"
#include "level/Doors.h"
#include "level/Objects.h"
#include "level/PropLinks.h"
#include "level/PropList.h"
#include "level/StageSetup.h"
#include "level/Stan.h"
#include "level/Waypoints.h"

namespace Frigate::Level {

// SPEED: how fast a character travels when an AI command sends it somewhere.
enum class Speed : int { Walk, Run, Sprint };

// What the AI reads of the player (getCurrentPlayerProp and the bondview and
// player globals the commands test).
struct AiPlayer {
    // The player's slot in the prop list.
    int prop = -1;
    // bondviewGetPlayerYawRadians.
    float yaw = 0.0f;
    // currentPlayerGetHealth, 0 to 1.
    float health = 1.0f;
    // bondviewGetVisibleToGuardsFlag.
    bool visibleToGuards = true;
    // isBondInTank.
    bool inTank = false;
    // bonddead.
    bool dead = false;
};

// The action the AI asks of a character: the part of chraction.c that starts
// or drives a character's animation, movement or attack. The default of each
// does nothing and, where it answers, answers that the character could not
// (the TRY commands then fall through to their next command). ChrActions
// implements them.
class AiActions {
  public:
    virtual ~AiActions() = default;

    // The tick of the character's current action (the switch of
    // chrlvActionTick that follows its AI list), run when its sleep is over.
    virtual void Tick(Chr& chr);

    // The animation each command starts once the character is able to act
    // (ChrAi checks chrIsNotDeadOrShot first): chrlvKneelingAnimationRelated7F023E48
    // (AI_Stop), chrKneelChooseAnimation, chrlvActorShuffleFeet
    // (AI_PointAtBond), chrlvActorLookFlustered, chrlvSurrenderAnimationRelated
    // (AI_RaiseArms), chrlvActorThrowWeaponSurrender, chrlvActorFadeAway and
    // chrStartAlarmChooseAnimation.
    virtual void Stop(Chr& chr);
    virtual void Kneel(Chr& chr);
    virtual void PointAtBond(Chr& chr);
    virtual void LookSurprised(Chr& chr);
    virtual void RaiseArms(Chr& chr);
    virtual void Surrender(Chr& chr);
    virtual void FadeOut(Chr& chr);
    virtual void StartAlarm(Chr& chr);
    // chrlvPerformAnimationForActor.
    virtual void PlayAnimation(Chr& chr, int animation, int startFrame, int endFrame, uint8_t bitfield,
                               int interpolationTicks);
    // AI_PlayAnimation for an aircraft: modelSetAnimation with the aircraft's
    // table and its end frame.
    virtual void PlayAircraftAnimation(PlacedObject& aircraft, int animation, int startFrame, int endFrame,
                                       int interpolationTicks);
    // actor_steps_sideways, actor_hops_sideways, actor_jogs_sideways.
    virtual bool Sidestep(Chr& chr);
    virtual bool SideHop(Chr& chr);
    virtual bool SideRun(Chr& chr);
    // actor_walks_and_fires, actor_runs_and_fires, actor_rolls_fires_crouched.
    virtual bool FiringWalk(Chr& chr);
    virtual bool FiringRun(Chr& chr);
    virtual bool FiringRoll(Chr& chr);
    // actor_aim_at_actor, actor_kneel_aim_at_actor,
    // actor_fire_or_aim_at_target_update and check_set_actor_standing_still:
    // `type` is the ATTACKTYPE_* bitfield and `id` the chr or pad it names.
    virtual bool AimAtTarget(Chr& chr, int type, int id);
    virtual bool KneelAimAtTarget(Chr& chr, int type, int id);
    virtual bool AimAtTargetUpdate(Chr& chr, int type, int id);
    virtual bool FaceTarget(Chr& chr, int type, int id);
    // actor_draws_throws_grenade_at_player_if_possible.
    virtual bool ThrowGrenade(Chr& chr);
    // plot_course_for_actor: the character sets out for `position`, standing
    // on `tile`.
    virtual bool PlotCourse(Chr& chr, const Vec3& position, int tile, Speed speed);
    // set_actor_on_path: the character patrols the path (an index into the
    // setup's patrol paths).
    virtual void StartPatrol(Chr& chr, int path);
    // handles_shot_actors for a shot an AI command makes: `target` is hit on
    // body part `part` by `item` along `direction`.
    virtual bool HitChr(Chr& target, int part, const Vec3& direction, int item);
    // handles_shot_actors: False when the character does not take the shot.
    virtual bool HandleShot(Chr& target, int part, const Vec3& direction, int item, bool fromPlayer);
    // chrlvExplosionDamage: an explosion at `position` does `damage` to the
    // character; `sound` lets it cry out. True when it kills the character.
    virtual bool ExplosionDamage(Chr& target, const Vec3& position, float damage, bool sound);
    // chrSetHiddenToRandom.
    virtual void FlinchRandomly(Chr& chr);
    // chrUpdateAimProperties: the smoothing of the aim angles, once a tick.
    virtual void UpdateAimProperties(Chr& chr);
    // chrlvTriggerFireWeapon: the shots the character's tick fires.
    virtual void TriggerFireWeapon(Chr& chr);
    // chrDropItems.
    virtual void DropItems(Chr& chr);
    // chrSetFiring for both hands, and chrlvResetAimend.
    virtual void StopFiring(Chr& chr);
    // The animation chrlvIdleAnimationRelated starts: the unarmed idle
    // (mirrored when `mirror`) when `unarmed`, otherwise the armed idle
    // (mirrored for a gun in the left hand).
    virtual void StartIdleAnimation(Chr& chr, float duration, bool unarmed, bool mirror);
};

// What the AI reads and changes of the rest of the game. The stage implements
// it; each method is the decomp function named with it.
class AiHost {
  public:
    virtual ~AiHost() = default;

    // The player.
    virtual AiPlayer Player() const = 0;
    // bondviewUpdateGuardTankFlagsRelated on the player's prop: takes the player,
    // and the tank the player drives, out of the line tests (`collides` false)
    // or puts them back.
    virtual void SetPlayerCollides(bool collides) {
        (void)collides;
    }
    // lvlGetSelectedDifficulty, bossGetStageNum.
    virtual int Difficulty() const = 0;
    virtual int StageNumber() const = 0;
    // lvlGetCurrentMultiPlayerSec and lvlGetCurrentMultiPlayerMin.
    virtual float StageSeconds() const = 0;
    virtual float PowerOnSeconds() const = 0;
    // D_80048380: the frames run while the controls were not locked.
    virtual int Frame() const {
        return 0;
    }
    // getPlayerCount.
    virtual int PlayerCount() const = 0;
    // fogGetScaledFarFogIntensitySquared.
    virtual float FarFogIntensitySquared() const = 0;
    // getROOMID_isRendered: the room is drawn this frame.
    virtual bool RoomRendered(int room) const = 0;
    // bgRoomsSharePortal.
    virtual bool RoomsSharePortal(int roomA, int roomB) const = 0;
    // chrIsPosOffScreen.
    virtual bool PositionOffScreen(const Vec3& position, int tile) const = 0;
    // The test sub_GAME_7F0333F8 makes of the player's view: whether the ray
    // from the eye along the crosshair passes through a sphere of `radius`
    // around the world position `centre` (projectileTestPropBoundingSphere).
    virtual bool PlayerAimsAtSphere(const Vec3& centre, float radius) const = 0;

    // The alarm (alarmActivate, alarmDeactivate, alarmIsActive) and the toxic
    // gas (check_if_toxic_gas_activated, init_trigger_toxic_gas_effect).
    virtual void AlarmActivate() = 0;
    virtual void AlarmDeactivate() = 0;
    virtual bool AlarmActive() const = 0;
    virtual bool GasLeaking() const = 0;
    virtual void GasLeak() = 0;
    // get_civilian_casualties.
    virtual int CiviliansKilled() const = 0;

    // The objectives: objectiveGetCount, objectiveGetStatus and
    // objectiveIsAllComplete.
    virtual int ObjectiveCount() const = 0;
    virtual bool ObjectiveComplete(int objective) const = 0;
    virtual bool AllObjectivesComplete() const = 0;

    // The player's weapons: getCurrentPlayerWeaponId, currentPlayerGetAmmoCount,
    // currentPlayerEquipWeaponWrapper, currentPlayerUnEquipWeaponWrapper and
    // remove_item_in_hand. `hand` is Hand.
    virtual int PlayerWeapon(int hand) const = 0;
    virtual int PlayerAmmo(int item) const = 0;
    virtual void PlayerEquip(int hand, int item) = 0;
    virtual void PlayerUnequip(int hand, int item) = 0;
    virtual void PlayerRemoveItemInHand(int hand) = 0;
    // bondinvHasPropInInv.
    virtual bool PlayerHasProp(int prop) const = 0;
    // bondwalkItemCheckBitflags.
    virtual bool WeaponHasFlag(int item, uint32_t flag) const = 0;

    // Text: hudmsgBottomShow and hudmsgTopShow of langGet(text).
    virtual void ShowBottomText(int text) = 0;
    virtual void ShowTopText(int text) = 0;
    // What AI_BondDisableControl and AI_BondEnableControl switch off and on:
    // the crosshair and ammo display, the HUD messages, the upper text and the
    // countdown. `flags` is the command's PLAYERFLAG_* byte.
    virtual void BondControl(bool enable, uint8_t flags) = 0;
    // The mission countdown (countdownTimer*).
    virtual void CountdownShow(bool show) = 0;
    virtual void CountdownSet(float ticks) = 0;
    virtual void CountdownRunning(bool running) = 0;
    virtual bool CountdownIsRunning() const = 0;
    virtual float CountdownValue() const = 0;

    // The camera (bondviewGetCameraMode and bondviewSetCameraMode, with what
    // the AI cutscene commands set up first).
    virtual int CameraMode() const = 0;
    virtual void CameraReturnToBond() = 0;
    virtual void CameraLookAtBondFromPad(int pad) = 0;
    virtual void CameraSwitch(const PropDef& cameraPosition, int lookAtBond, int unused) = 0;
    virtual void CameraOrbitPad(int pad, int latDistance, int vertDistance, int orbitSpeed60, int yOffset,
                                int initialRotation) = 0;
    // currentPlayerSetFadeColour and currentPlayerSetFadeFrac, and whether the
    // fade has finished (colourfadetimemax60 < 0).
    virtual void ScreenFade(bool toBlack) = 0;
    virtual bool ScreenFadeCompleted() const = 0;
    // g_ForceBondMoveOffset.
    virtual void BondForceMove(float x, float z) = 0;
    // fogSwitchToSolosky2.
    virtual void SwitchSky() = 0;
    // SurroundWithExplosions.
    virtual void ExplodeAroundBond() = 0;
    // fileGetBondForCurrentFolder.
    virtual int FolderActor() const = 0;
    // aiEndLevel: bossReturnTitleStage, or the camera-buffer flag it sets
    // first.
    virtual void EndLevel() = 0;
    // stop_time_flag, credits_state, g_PlayerInvincible and g_isBondKIA.
    virtual int StopTime() const = 0;
    virtual void SetStopTime(int state) = 0;
    virtual int Credits() const = 0;
    virtual void SetCredits(int state) = 0;
    virtual bool PlayerInvincible() const = 0;
    virtual void SetPlayerInvincible(bool invincible) = 0;
    virtual void SetBondKilledInAction() = 0;
    // cheatIsActive.
    virtual bool CheatActive(int cheat) const {
        (void)cheat;
        return false;
    }

    // The sound the AI starts and stops: audioPlayFromProp, sub_GAME_7F0349BC,
    // and the channel table (sfx_related) the volume commands edit.
    virtual void SfxPlay(int channel, int sound) = 0;
    virtual void SfxStop(int channel) = 0;
    virtual void SfxSetVolume(int channel, int volume, int fadeTicks) = 0;
    virtual void SfxFadeVolume(int channel, float volume, int fadeTicks) = 0;
    virtual void SfxEmitFromProp(int channel, int prop, int fadeTicks) = 0;
    virtual void SfxEmitFromPad(int channel, int pad, int fadeTicks) = 0;
    virtual int SfxChannelVolume(int channel) const = 0;
    // musicPlaySlot and musicStopSlot.
    virtual void MusicPlaySlot(int slot, int stoppedSeconds, int totalSeconds) = 0;
    virtual void MusicStopSlot(int slot) = 0;

    // Objects the AI drives.
    // propobjInteract for an object prop.
    virtual void UseObject(int prop) = 0;
    // objApplyDamage with the damage AI_DestroyObject works out.
    virtual void DestroyObject(PlacedObject& object) = 0;
    // propobjSetDropped(prop, how) and chrDropItems.
    virtual void DropProp(int prop, int how) = 0;
    virtual void DropConcealedItems(Chr& chr) = 0;
    // propPickupByPlayer and propExecuteTickOperation.
    virtual void CollectProp(int prop) = 0;
    // The detach, chrEquipWeapon and chrpropReparent of AI_ChrEquipObject.
    virtual void EquipObject(int prop, Chr& chr) = 0;
    // AI_MoveObject: sub_GAME_7F04088C and setupUpdateObjectRoomPosition.
    virtual void MoveObjectToPad(PlacedObject& object, int pad) = 0;
    // weaponFindThrown.
    virtual bool KeyDropped(int key) const = 0;
    // monitorSetImageByNum.
    virtual void SetMonitorImage(PlacedObject& monitor, int slot, int bank) = 0;
    // The projectile AI_ObjectRocketLaunch starts.
    virtual void LaunchObject(PlacedObject& object) = 0;

    // What characters carry. chrGiveWeapon, hatCreateForChr, chrDropItem (a
    // new item model dropped at the character), the clone's copy of its
    // original's gear, chrSetWeaponFlag4 and chrGetEquippedWeaponPropWithCheck
    // (the item held in `hand`, or -1 when it is not a gun).
    virtual bool GiveWeapon(Chr& chr, int model, int item, uint32_t flags) = 0;
    virtual bool GiveHat(Chr& chr, int model, uint32_t flags) = 0;
    virtual bool DropNewItem(Chr& chr, int model, int item) = 0;
    virtual void CloneChr(Chr& source, Chr& clone) = 0;
    virtual void RemoveHeldItem(Chr& chr, int hand) = 0;
    virtual int HeldGunItem(const Chr& chr, int hand) const = 0;

    // Combat. The weapon table (null before the stage has one), and the
    // difficulty's scales for what the enemies' shots do (g_AiAccuracyModifier,
    // g_AiDamageModifier and g_AiHealthModifier).
    virtual const WeaponTable* Weapons() const = 0;
    virtual float AiAccuracy() const = 0;
    virtual float AiDamage() const = 0;
    virtual float AiHealth() const = 0;
    // g_AiReactionSpeed.
    virtual float AiReactionSpeed() const = 0;
    // get_007_health_mod, get_007_damage_mod, get_007_accuracy_mod and
    // get_007_reaction_speed: the 007 options screen's sliders on the 007
    // difficulty, 1, 1, 1 and 0 on every other.
    virtual float Ai007HealthMod() const = 0;
    virtual float Ai007DamageMod() const = 0;
    virtual float Ai007AccuracyMod() const = 0;
    virtual float Ai007ReactionSpeed() const = 0;
    // bondviewGetPlayerDuckingHeightRelated.
    virtual float PlayerDuckingHeight() const = 0;
    // currentPlayerGetViewToWorldMtxf.
    virtual const Mtxf& ViewToWorld() const = 0;
    // currentPlayerGetMatrix10EC: the view-to-world matrix before the last
    // time the camera was set up.
    virtual const Mtxf& PreviousViewToWorld() const = 0;
    // bondviewGetIfCurrentPlayerDamageShowTime: the player is flashing from a
    // hit, and takes no more until it is over.
    virtual bool PlayerDamageShowing() const = 0;
    // bondviewCallRecordDamageKills(damage, angle, -1, TRUE): a character's
    // shot hits the player, from the direction `angle`.
    virtual void DamagePlayer(float damage, float angle) = 0;
    // inc_curplayer_hitcount_with_weapon, with a SHOT_REGISTER.
    virtual void CountPlayerShot(int item, int shotRegister) = 0;
    // recall_joy2_hits_edit_detail_edit_flag: `item` hits the prop at `position`
    // (a character or the viewer when `fleshy`), and recall_joy2_hits_edit_flag:
    // it hits the world at `position`.
    virtual void PropHitSound(int item, const Vec3& position, bool fleshy) = 0;
    virtual void WorldHitSound(int item, const Vec3& position) = 0;
    // bullet_spark_create.
    virtual void CreateSpark(const Vec3& position, int type, float size, int room) = 0;
    // chrobjMaybeDetonateObjectIfFlags.
    virtual void ShootObject(PlacedObject& object, float damage, const Vec3& position, int item) = 0;
    // The rocket or grenade round a character fires (create_new_item_instance_of_model
    // and gunInitProjectileObject): from `origin` on tile `tile`, along `direction`
    // and turned as `heading` and `elevation` aim it.
    virtual void LaunchRound(Chr& chr, int item, const Vec3& origin, int tile, const Vec3& direction, float elevation,
                             float heading) = 0;
    // The kill counters a character's death adds to:
    // increment_num_kills_display_text_in_MP (the player's kill count; its
    // on-screen count and timing records are multiplayer's) and
    // inc_cur_civilian_casualties.
    virtual void CountKill() = 0;
    virtual void CountCivilianCasualty() = 0;
};

// What the AI reads and changes of the level that the level code already
// models.
struct AiScene {
    const StageSetup* setup = nullptr;
    Stan* stan = nullptr;
    PropList* props = nullptr;
    Chrs* chrs = nullptr;
    Objects* objects = nullptr;
    Doors* doors = nullptr;
    PropLinks* links = nullptr;
    Waypoints* paths = nullptr;
    AiLists* lists = nullptr;
};

// The state the AI keeps that is not a character's.
struct AiState {
    // objectiveregisters1: the bits the AI commands set and test.
    uint32_t objectiveRegisters = 0;
    // g_SeenBondRecentlyGuardCount.
    int seenBondRecentlyGuards = 0;
};

// The AI list interpreter (chrai.c's ai()) and the sight, hearing and
// distance tests its commands make (chraction.c).
class ChrAi {
  public:
    ChrAi(const AiScene& scene, AiHost& host, AiActions& actions) : scene_(scene), host_(host), actions_(actions) {
    }

    // ai(chr, PROP_TYPE_CHR): runs `chr`'s list until it yields.
    void Run(Chr& chr);
    // ai(object, PROP_TYPE_OBJ) for a vehicle or aircraft object.
    void RunObject(PlacedObject& object);

    // chrlvActionTick: one tick of a character's timers, AI list and action.
    void ActionTick(Chr& chr);
    // chrlvAllChrTick: ticks the background characters, then counts the
    // characters that saw the player recently.
    void AllChrTick();

    AiState& State() {
        return state_;
    }
    const AiState& State() const {
        return state_;
    }

    // The tests and updates of chraction.c the commands use; public so the
    // action code can use the same ones.
    bool IsDead(const Chr& chr) const;
    bool IsNotDeadOrShot(const Chr& chr) const;
    bool CheckTargetInSight(Chr& chr);
    bool CanSeeBond(Chr& chr);
    bool IsTargetNearlyInSight(Chr& chr);
    bool SawTargetRecently(const Chr& chr) const;
    bool HeardTargetRecently(const Chr& chr) const;
    bool IsHearingBond(const Chr& chr) const;
    bool PositionInSameRoom(Chr& chr, const Vec3& position, int tile);
    bool HasStoppedOrPatrolling(const Chr& chr) const;
    float DistanceToBond(const Chr& chr) const;
    float AngleToBond(const Chr& chr) const;
    float AngleFromBond(const Chr& chr) const;
    // get_007_reaction_speed.
    float ReactionSpeed() const;
    // chrlvGetGuard007SpeedRating: the character's speed rating, with the 007
    // difficulty's reaction slider, scaled from `min` (rating 0) to `max`
    // (rating 100).
    float SpeedRating(const Chr& chr, float min, float max) const;
    // chrSetMoving: `unset` clears CHRHIDDEN_MOVING.
    static void SetMoving(Chr& chr, bool unset);
    // chrlvSetTargetToPlayer and chrlvAlertGuardToPlayerPosition.
    void SetTargetToPlayer(Chr& chr);
    void AlertToPlayer(Chr& chr);
    // chrCheckGuardsHeardSound: every character within `noise` (in hundreds of
    // units, scaled by its hearing) of the player is alerted to them.
    void GuardsHeardSound(float noise);
    // chrlvMergeKneelToStand: the character stands (ACT_STAND) after
    // `mergeTime` ticks.
    void MergeKneelToStand(Chr& chr, float mergeTime);
    // chrlvIdleAnimationRelated: the idle animation for what the character
    // holds, merged in over `mergeTime` ticks.
    void StartIdle(Chr& chr, float mergeTime);
    // The pad a pad id names (a plain pad, or a bound pad from 10000 on);
    // null for an id with none.
    const Pad* PadForId(int padId) const;

  private:
    struct Subject;
    // The interpreter loop for one entity.
    void Execute(Subject& subject);
    // The commands' shared helpers.
    Vec3 PlayerPosition() const;
    int PlayerTile() const;
    float DistanceToPad(const Chr& chr, int padId) const;
    float DistanceToChr(const Chr& chr, int id);
    float DistanceFromBondToPad(const Chr& chr, int padId) const;
    bool PlayerCanSee(const Chr& chr);
    // sub_GAME_7F0304AC.
    bool LineViaPoint(Chr& chr, const Vec3& from, int fromTile, const Vec3& via, const Vec3& to, int toTile);
    bool GoToBond(Chr& chr, Speed speed);
    bool GoToChr(Chr& chr, int id, Speed speed);
    bool GoToPad(Chr& chr, int padId, Speed speed);
    int PathQuadrantPad(const Vec3& position, int tile, float rotation, uint8_t quadrant);
    bool SetPadPresetByQuadrant(Chr& chr, uint8_t quadrant);
    bool SetPresetToPlayerNeighbour(Chr& chr, uint8_t padnum);
    bool SetChrPresetNear(Chr& chr, float distance);
    bool SpawnAtCoord(int body, int head, const Vec3& position, int tile, float angle, const AiList* list,
                      uint32_t flags, Chr** spawned);
    bool SpawnAtPad(const Chr& self, int body, int head, int pad, const AiList* list, uint32_t flags, Chr** spawned);
    bool SpawnAtChr(Chr& self, int body, int head, int chrnum, const AiList* list, uint32_t flags, Chr** spawned);
    bool TeleportChrToPad(Chr& self, Chr& target, int pad);
    bool StartAlarmAt(Chr& chr, int pad);
    bool Clone(Chr& self, const AiCommand& command);
    int SpeedRatingInt(const Chr& chr, int scale) const;

    AiScene scene_;
    AiHost& host_;
    AiActions& actions_;
    AiState state_;
};

} // namespace Frigate::Level
