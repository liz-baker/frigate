#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/ChrRecords.h"
#include "game/SoundHandle.h"
#include "level/Beams.h"
#include "level/ChrCombatTables.h"
#include "level/AiLists.h"
#include "level/ModelHandle.h"
#include "level/PlayerWeapons.h"
#include "level/PropList.h"
#include "level/Shading.h"
#include "level/Stan.h"
#include "level/StanProps.h"
#include "level/StageSetup.h"

namespace Frigate::Level {

// CHRFLAG_*.
namespace ChrFlag {
constexpr uint32_t Init = 0x00000001;
constexpr uint32_t Clone = 0x00000002;
// CHRFLAG_NEAR_MISS: a shot just missed the character; cleared every tick.
constexpr uint32_t NearMiss = 0x00000004;
constexpr uint32_t HasBeenOnScreen = 0x00000008;
constexpr uint32_t Invincible = 0x00000010;
// CHRSTART_FORCENOBLOOD.
constexpr uint32_t ForceNoBlood = 0x00000020;
// CHRFLAG_CAN_SHOOT_CHRS: the character's shots hurt other characters.
constexpr uint32_t CanShootChrs = 0x00000040;
// CHRFLAG_00000080: the decomp does not name it; a standing character carrying
// it never turns to face a clear wall.
constexpr uint32_t Unnamed0x80 = 0x00000080;
// CHRFLAG_WAS_DAMAGED.
constexpr uint32_t WasDamaged = 0x00000100;
// CHRFLAG_00000200: the decomp does not name it; a character in ACT_ARGH
// carrying it still counts as able to act.
constexpr uint32_t Unnamed0x200 = 0x00000200;
// CHRFLAG_NO_AUTOAIM: the gun does not swing to the character.
constexpr uint32_t NoAutoAim = 0x00000800;
// CHRFLAG_NO_SHADOW: the character draws no shadow.
constexpr uint32_t NoShadow = 0x00002000;
// CHRFLAG_IGNORE_ANIM_TRANSLATION: animations do not move the character.
constexpr uint32_t IgnoreAnimTranslation = 0x00004000;
// CHRFLAG_LOCK_Y_POS: the chr's height is not taken from the tile it stands on.
constexpr uint32_t LockYPos = 0x00001000;
// CHRFLAG_IMPACT_ALWAYS: a shot throws the character back whatever the weapon.
constexpr uint32_t ImpactAlways = 0x00008000;
// CHRFLAG_COUNT_DEATH_AS_CIVILIAN.
constexpr uint32_t CountDeathAsCivilian = 0x00100000;
constexpr uint32_t Hidden = 0x00000400;
// CHRFLAG_00010000: the decomp does not name it; chrUpdateCollisionBounds
// gives a character carrying it no footprint.
constexpr uint32_t Unnamed0x10000 = 0x00010000;
// CHRFLAG_00020000: the character draws in the blended pass only.
constexpr uint32_t Unnamed0x20000 = 0x00020000;
// CHRFLAG_00040000: the character's action tick runs even while it sleeps or is
// hidden.
constexpr uint32_t Unnamed0x40000 = 0x00040000;
// CHRFLAG_INCREASE_RUNNING_SPEED: the character sprints faster.
constexpr uint32_t IncreaseRunningSpeed = 0x00080000;
// CHRFLAG_WAS_HIT: the character was hit, invincible or not.
constexpr uint32_t WasHit = 0x00200000;
// CHRFLAG_CULL_USING_HITBOX: the character is culled by its hitbox rather than
// its tile.
constexpr uint32_t CullUsingHitbox = 0x00800000;
// CHRFLAG_02000000: an animation's sound cue has played.
constexpr uint32_t SoundCuePlayed = 0x02000000;
// CHRFLAG_04000000: the character is exempt from the fog fade.
constexpr uint32_t NoFade = 0x04000000;
} // namespace ChrFlag

// CHRHIDDEN_*.
namespace ChrHidden {
constexpr uint32_t None = 0;
// CHRHIDDEN_DROP_HELD_ITEMS.
constexpr uint32_t DropHeldItems = 0x0001;
// CHRHIDDEN_ALERT_GUARD_RELATED: the character heard the player this tick.
constexpr uint32_t AlertGuard = 0x0002;
// CHRHIDDEN_FIRE_WEAPON_LEFT and _RIGHT: the weapon in that hand fires on the
// character's next tick.
constexpr uint32_t FireWeaponLeft = 0x0004;
constexpr uint32_t FireWeaponRight = 0x0008;
// CHRHIDDEN_FIRE_TRACER.
constexpr uint32_t FireTracer = 0x0080;
// CHRHIDDEN_0400: the torso aims no further than the head can follow.
constexpr uint32_t Unnamed0x400 = 0x0400;
// CHRHIDDEN_RAND_FLINCH_1 to _8: which way a flinch throws the character's
// arms and back.
constexpr uint32_t RandFlinch1 = 0x1000;
constexpr uint32_t RandFlinch2 = 0x2000;
constexpr uint32_t RandFlinch4 = 0x4000;
constexpr uint32_t RandFlinch8 = 0x8000;
constexpr uint32_t RandFlinchMask = 0xF000;
// CHRHIDDEN_OFFSCREEN_PATROL: a door closing on the character made it walk off
// its patrol out of sight.
constexpr uint32_t OffscreenPatrol = 0x0010;
// CHRHIDDEN_REMOVE: the character is removed on its next tick.
constexpr uint32_t Remove = 0x0020;
// CHRHIDDEN_TIMER_ACTIVE: timer60 is counting.
constexpr uint32_t TimerActive = 0x0040;
constexpr uint32_t Moving = 0x0100;
// CHRHIDDEN_BACKGROUND_AI: the character has an AI script running.
constexpr uint32_t BackgroundAi = 0x0200;
// CHRHIDDEN_FREEZE: the current animation state is held.
constexpr uint32_t Freeze = 0x0800;
} // namespace ChrHidden

// ACT_TYPE.
namespace ChrAction {
constexpr int Init = 0;
constexpr int Stand = 1;
constexpr int Kneel = 2;
constexpr int Anim = 3;
constexpr int Die = 4;
constexpr int Dead = 5;
constexpr int Argh = 6;
constexpr int PreArgh = 7;
constexpr int Attack = 8;
constexpr int AttackWalk = 9;
constexpr int AttackRoll = 10;
constexpr int Sidestep = 11;
constexpr int JumpOut = 12;
constexpr int RunPos = 13;
constexpr int Patrol = 14;
constexpr int GoPos = 15;
constexpr int Surrender = 16;
constexpr int LookAtTarget = 17;
constexpr int Surprised = 18;
constexpr int StartAlarm = 19;
constexpr int ThrowGrenade = 20;
constexpr int TurnDir = 21;
constexpr int Test = 22;
constexpr int BondIntro = 23;
constexpr int BondDie = 24;
constexpr int BondMulti = 25;
// The action of the stage's background AI characters.
constexpr int Null = 26;
} // namespace ChrAction

// WAYMODE_*: the modes of a character's route following. The numbered ones
// are the states of its steering round obstacles; Magic moves a character no
// one sees straight along its route.
namespace WayMode {
constexpr int8_t Mode0 = 0;
constexpr int8_t Mode1 = 1;
constexpr int8_t Mode2 = 2;
constexpr int8_t Mode3 = 3;
constexpr int8_t Mode4 = 4;
constexpr int8_t Mode5 = 5;
constexpr int8_t Magic = 6;
} // namespace WayMode
// MAX_CHRWAYPOINTS, and MAX_WAYMODE (the ticks a mode is tried for).
constexpr int kMaxChrWaypoints = 6;
constexpr int kMaxWayMode = WayMode::Magic;

// The chrnum of the background AI characters (g_ActiveChrs).
constexpr int kBackgroundChrnum = 0xFE;
// The special chr ids AI commands use (CHR_*), as the signed values the decomp
// defines; commands carry them as bytes.
constexpr int kChrBondCinema = -8;
constexpr int kChrClone = -7;
constexpr int kChrSeeShot = -6;
constexpr int kChrSeeDie = -5;
constexpr int kChrPreset = -4;
constexpr int kChrSelf = -3;
// CHR_FREE: a chrseeshot or chrseedie with nobody in it.
constexpr int kChrFree = -1;
// PAD_PRESET1: a pad id that names the character's own preset pad.
constexpr int kPadPreset1 = 9000;

// GUARD_SETUP_FLAG_*: GuardRecord::bitflags.
namespace GuardSetupFlag {
constexpr uint16_t Sunglasses = 0x0001;
constexpr uint16_t RandomSunglasses = 0x0002;
constexpr uint16_t Clone = 0x0004;
constexpr uint16_t Invincible = 0x0008;
} // namespace GuardSetupFlag

// The frame accounting a spawn reads (g_GlobalTimer, g_GlobalTimerDelta,
// g_ClockTimer and D_80048380, the frames run while the controls were free).
struct ChrTiming {
    int globalTimer = 0;
    float globalTimerDelta = 1.0f;
    int clockTimer = 1;
    int frame = 0;
};

// ChrRecord: a slot of g_ChrSlots. Only the fields the spawn, the AI, the
// combat actions and the prop collision tests read are here.
struct Chr {
    // model != NULL in the decomp: the slot is taken.
    bool inUse = false;
    int16_t chrnum = 0;
    int8_t headnum = 0;
    int8_t bodynum = 0;
    // Whether the head's sunglasses show (retrieve_header_for_body_and_head).
    bool sunglasses = false;
    // The chr's slot in the prop list.
    int prop = -1;
    uint32_t chrflags = 0;
    uint32_t hidden = 0;
    int actionType = ChrAction::Init;
    // sleep: ticks until the AI next runs; an s8, so it wraps as the game's.
    int8_t sleep = 0;
    // ailist, aioffset and aireturnlist. The list is null when the character
    // has none to run.
    const AiList* aiList = nullptr;
    uint16_t aiOffset = 0;
    int16_t aiReturnList = -1;
    // morale, alertness, flags2 and random: what the AI commands keep on the
    // character.
    uint8_t morale = 0;
    uint8_t alertness = 0;
    uint8_t flags2 = 0;
    uint8_t random = 0;
    int32_t timer60 = 0;
    int8_t accuracyRating = 0;
    int8_t speedRating = 0;
    int8_t arghRating = 0;
    int8_t numArghs = 0;
    int8_t numCloseArghs = 0;
    // beams: the bullet beams of the right and left hands' shots.
    std::array<BeamRecord, 2> beams;
    // lastseetarget60, lastheartarget60, lastknowntargetpos and targetTile:
    // when the character last saw or heard its target and where it was then.
    int32_t lastSeeTarget60 = 0;
    int32_t lastHearTarget60 = 0;
    Vec3 lastKnownTargetPos{};
    int targetTile = kNoTile;
    // seen_bond_time: g_GlobalTimer when the character last saw the player.
    int32_t seenBondTime = 0;
    // g_GlobalTimer when the character last walked (lastwalk60).
    int32_t lastWalk60 = 0;
    // The state of the actions below, as the decomp's act_* union holds it.
    struct ActStand {
        int32_t prestand = 0;
        int32_t faceEntityType = 0;
        int32_t faceEntityId = 0;
        int32_t reaim = 0;
        int32_t turning = 0;
        uint32_t checkFacingWall = 0;
        int32_t wallCount = 0;
    } actStand;
    // act_anim: the flags PlayAnimation's bitfield sets.
    struct ActAnim {
        // ANIM_UNKNOWN.
        bool unnamed2C = false;
        // ANIM_LOOP_HOLD_LAST_FRAME: the animation holds its last frame instead of
        // ending in the idle pose.
        bool holdLastFrame = false;
        // ANIM_PLAY_SFX.
        bool playSfx = false;
        // ANIM_IDLE_POSE_WHEN_COMPLETE.
        bool idleOnEnd = false;
        // ANIM_NO_TRANSLATION.
        bool noTranslate = false;
    } actAnim;
    // act_dead: the fade-out. Negative until the first tick, then the ticks
    // spent fading (allowfade).
    struct ActDead {
        int32_t fadeTicks = 0;
    } actDead;
    // act_runpos: where the character runs and when it stops.
    struct ActRunPos {
        Vec3 position{};
        float nearDistance = 0.0f;
        int32_t eta60 = 0;
        float turnSpeed = 0.0f;
    } actRunPos;
    // Which of a character's route-finding states it is in (WAYMODE_*): the
    // modes a detour round an obstacle goes through, and the "magic" one that
    // moves a character no one sees straight along its route.
    // act_patrol and act_gopos's waydata.
    struct WayData {
        int8_t mode = 0;
        // unk01: ticks spent in the current detour mode.
        int8_t modeTicks = 0;
        // unk02 and unk03: `pos` and `posCopy` hold a point to steer round.
        int8_t hasPos = 0;
        int8_t hasPosCopy = 0;
        Vec3 pos{};
        Vec3 pos2{};
        Vec3 pos3{};
        int32_t age = 0;
        Vec3 posCopy{};
        float segDistDone = 0.0f;
        float segDistTotal = 0.0f;
    };
    // act_patrol: the patrol path (an index into Waypoints::Paths()), the step
    // being walked to and the direction along the path.
    struct ActPatrol {
        int path = -1;
        int32_t nextStep = 0;
        bool forward = true;
        WayData way;
        int32_t lastVisible60 = 0;
        float speed = 0.0f;
    } actPatrol;
    // act_gopos: the position being run to, the route to it (waypoints, ended
    // by -1) and how far along it the character is.
    struct ActGoPos {
        Vec3 targetPosition{};
        int targetTile = kNoTile;
        int targetWaypoint = -1;
        std::array<int, 6> waypoints{ -1, -1, -1, -1, -1, -1 };
        uint8_t currentIndex = 0;
        // unk59: the SPEED the character moves at.
        uint8_t speedMode = 0;
        // unk5a: ticks until the route is planned again.
        uint16_t restartTicks = 0;
        WayData way;
        // unk9c: g_GlobalTimer when the character last stepped where it was seen.
        int32_t lastVisible60 = 0;
        float speed = 0.0f;
    } actGoPos;
    // act_attack, which ACT_ATTACKROLL shares (the roll sets attackType to
    // TARGET_BOND and the rest to what the union holds in their place).
    struct ActAttack {
        // animfloats: the firing animation being played.
        const FiringAnim* anim = nullptr;
        // unk30: the turn towards the target (see TurnToFace's states).
        int8_t turnState = 0;
        // unk31: the next shot is due.
        int8_t shotDue = 0;
        // unk32: the hand that fires next.
        int8_t nextHand = 0;
        // unk33 and unk34: the shots fired and the shots to fire in a burst.
        int8_t shotsFired = 0;
        int8_t shotsPlanned = 0;
        // unk35: a roll that fires two guns.
        int8_t dualRoll = 0;
        // unk36: no gun held is automatic or a laser; unk37: some gun is automatic.
        int8_t allSingleShot = 0;
        int8_t anyAutomatic = 0;
        // unk38, unk3a and unk3c, by hand: the hand fires, its gun fires single
        // shots, and its gun is a launcher.
        std::array<int8_t, 2> fires{};
        std::array<int8_t, 2> single{};
        std::array<int8_t, 2> launcher{};
        // unk40: ticks a roll's shots have been slowed for.
        int32_t slowedTicks = 0;
        // unk44 and attack_time: g_GlobalTimer when a shot was last started and
        // when one last reached the player.
        int32_t lastShotTime = 0;
        int32_t attackTime = 0;
        // attacktype and entityid: the TARGET_* bits and the chr or pad they name.
        int32_t attackType = 0;
        int32_t entityId = 0;
        // unk54: the character fires standing (1) rather than kneeling (0).
        int32_t standing = 0;
        // type_of_motion: 1 is a shot taken at a target the aim could not
        // reach, 2 the recovery from it.
        int32_t typeOfMotion = 0;
    } actAttack;
    // act_attackwalk: shooting on the move.
    struct ActAttackWalk {
        // clock_timer30 and clock_timer34: ticks walked and ticks to walk.
        int32_t walkedTicks = 0;
        int32_t walkLimit = 0;
        // unk038: the character has started moving.
        int32_t moving = 0;
        const FiringAnim* anim = nullptr;
        // timer40 and unk044: the walked tick the next shot waits for, and the
        // hand that fires next.
        int32_t nextShotTick = 0;
        int32_t nextHand = 0;
        // unk48, unk4a and unk4C, by hand, as ActAttack's fires, single and launcher.
        std::array<int8_t, 2> fires{};
        std::array<int8_t, 2> single{};
        std::array<int8_t, 2> launcher{};
        float speed = 0.0f;
    } actAttackWalk;
    // act_die: the death animation's cues and the knock-back it carries.
    struct ActDie {
        // notifychrindex: the next slot to tell that this character died.
        int32_t notifyChrIndex = 0;
        float thudFrame1 = -1.0f;
        float thudFrame2 = -1.0f;
        float timeExtra = 0.0f;
        float elapseExtra = 0.0f;
        Vec3 extraSpeed{};
    } actDie;
    // act_argh: the hit reaction. hitTick is g_GlobalTimer when it began.
    struct ActArgh {
        int32_t notifyChrIndex = 0;
        int32_t hitTick = 0;
    } actArgh;
    // act_preargh: the shot a character in the middle of a roll is still to
    // react to.
    struct ActPreArgh {
        Vec3 position{};
        float angle = 0.0f;
        int hitPart = 0;
        int weapon = 0;
    } actPreArgh;
    // act_throwgrenade.
    struct ActThrowGrenade {
        int32_t hand = 0;
    } actThrowGrenade;
    // firecount, by hand: the shots each hand's gun has been asked for.
    std::array<uint8_t, 2> fireCount{};
    // field_160 and field_178: the sounds of each hand's gunfire, and the
    // global timer before which the next shot of a gun with a sound trigger
    // rate stays silent.
    std::array<std::array<Game::Sound::Handle, 2>, 2> gunSounds;
    std::array<int32_t, 2> gunSoundTime{};
    // aimendcount, and the aim angles the character's arms and back move
    // towards (aimend...) and have reached (aimup...) over it.
    int8_t aimEndCount = 0;
    float aimUpLeftShoulder = 0.0f;
    float aimUpRightShoulder = 0.0f;
    float aimUpBack = 0.0f;
    float aimSideBack = 0.0f;
    float aimEndLeftShoulder = 0.0f;
    float aimEndRightShoulder = 0.0f;
    float aimEndBack = 0.0f;
    float aimEndSideBack = 0.0f;
    // shotbondsum: how close the character's shots at the player are to hitting.
    float shotBondSum = 0.0f;
    int16_t padPreset1 = -1;
    int16_t chrPreset1 = -1;
    int16_t chrSeeShot = -1;
    int16_t chrSeeDie = -1;
    float hearingScale = 1.0f;
    float visionRange = 250.0f;
    float damage = 0.0f;
    float maxDamage = 0.0f;
    float ground = 0.0f;
    float manGround = 0.0f;
    float sumGround = 0.0f;
    Vec3 fallSpeed{};
    Vec3 prevPosition{};
    float chrWidth = 20.0f;
    float chrHeight = 185.0f;
    int lastMoveOk60 = 0;
    int invalidMove = 0;
    int8_t flinchCount = -1;
    uint8_t grenadeProb = 0;
    // model: the character's instance. Null in a source that has no models.
    std::shared_ptr<ModelHandle> model;
    // weapons_held: the props of the weapons in the right and left hands
    // (Hand), -1 for none, and handle_positiondata_hat: the hat prop.
    std::array<int, 2> weaponsHeld{ -1, -1 };
    int hat = -1;
    // getinstsize: the model's bounding radius before its scale.
    float boundingRadius = 0.0f;
    // shadecol, nextcol and fadealpha.
    ShadeColour shadeColour{};
    ShadeColour nextColour{};
    uint8_t fadeAlpha = 0xFF;
    // field_20: the models the character draws this frame (its own, then the
    // weapons it holds and its hat), which chrTick builds and chrRenderProp
    // consumes.
    std::vector<std::shared_ptr<ModelHandle>> drawList;
};

// getsubroty: the heading of a character's model, in radians.
inline float ChrHeading(const Chr& chr) {
    if (chr.model && chr.model->Playback().hasRoot) {
        return chr.model->Playback().root.heading;
    }
    return 0.0f;
}

// The stage's characters: the part of proplvreset2 that spawns guards
// (init_guards, alloc_init_GUARDdata_entries, bodiesReset and
// expand_09_characters) and the character prop's collision footprint.
class Chrs {
  public:
    using Random = std::function<uint32_t()>;

    // player1_guardID's initial value: the first chrnum a spawn without a
    // record number takes.
    static constexpr int kFirstAnonymousChrnum = 5000;

    // init_guards. `records` is the body and head table; null when the
    // source has none, and no character spawns.
    // `hats` and `models` are null for a source with no hat placements or
    // models; a character then spawns without a model.
    void Reset(Stan* stan, PropList* props, std::shared_ptr<const Game::ChrRecords> records,
               std::shared_ptr<const Game::ChrHats> hats = nullptr, ModelFactory* models = nullptr);

    // alloc_init_GUARDdata_entries: `count` guards plus ten spare slots.
    void AllocateSlots(int count);
    // The lists ailistFindById searches when a spawn names one.
    void SetAiLists(const AiLists* lists) {
        aiLists_ = lists;
    }
    // alloc_false_GUARDdata_to_exec_global_action: one background character
    // per background AI list of the setup (id 4096 and up), each running that
    // list.
    void CreateBackgroundChrs();

    // bodiesReset: draws the stage's random head and body choices.
    void BodiesReset(const Random& random);
    // get_007_health_mod, which scales the health a character spawns with.
    void SetHealthMod(float healthMod) {
        healthMod_ = healthMod;
    }

    // expand_09_characters for propdef record `def` of `setup`. False when
    // the guard's pad has no room for it, or it has nothing to spawn from.
    bool Spawn(const StageSetup& setup, size_t def, const ChrTiming& timing, const Random& random);
    // Whether a position is out of the player's sight (chrIsPosOffScreen).
    using OffScreenFn = std::function<bool(const Vec3& position, int tile)>;
    // chrAdjustPosForSpawn: moves `position` (and `tile` with it) to the nearest
    // of eight spots 60 units around it where a character fits and is not seen
    // unless `allowOnScreen`. False when there is none.
    bool AdjustPosForSpawn(Vec3* position, int* tile, float facing, bool allowOnScreen, const OffScreenFn& offScreen);
    // chrSpawnAtCoord: a character of `body` and `head` (negative picks one)
    // at `position`, running `list`. Returns its slot, -1 when the slots or
    // the space are used up. `flags` are the GUARD_SETUP_FLAG bits plus
    // 0x10, which allows a spot in view.
    int SpawnAtCoord(int body, int head, const Vec3& position, int tile, float angle, const AiList* list,
                     uint32_t flags, const ChrTiming& timing, const Random& random, const OffScreenFn& offScreen);
    // The PROPDEF_GUARD_ATTRIBUTE record's effect on the character it names.
    void ApplyAttribute(const PropDef& def);

    // chrFindByLiteralId.
    const Chr* FindByLiteralId(int chrnum) const;
    Chr* FindByLiteralId(int chrnum);
    // chrResolveId: the chrnum a special id (CHR_SELF, CHR_PRESET, ...) names
    // for `self`; any other id is itself.
    static int ResolveId(const Chr& self, int id);
    // chrFindById: the character `id` names for `self`, among the stage's
    // characters and then the background ones. Null when there is none.
    Chr* FindById(const Chr& self, int id);
    // chrResolvePadId: PAD_PRESET1 is the character's own preset pad.
    static int ResolvePadId(const Chr& self, int padId);
    // The background AI characters (g_ActiveChrs).
    std::vector<Chr>& Background() {
        return background_;
    }
    size_t BackgroundCount() const {
        return background_.size();
    }
    Chr& operator[](int slot) {
        return chrs_[static_cast<size_t>(slot)];
    }
    // The character spawned from propdef record `def`, or null.
    const Chr* ForDef(size_t def) const;
    const std::vector<Chr>& Slots() const {
        return chrs_;
    }
    std::vector<Chr>& Slots() {
        return chrs_;
    }
    // chrGetNumFree.
    int FreeSlots() const;
    const Chr& operator[](int slot) const {
        return chrs_[static_cast<size_t>(slot)];
    }

    // propIsOfCdType and chraiGetCollisionBounds for a character's prop: false
    // when `cdTypes` does not select characters or the character has no
    // footprint (chrUpdateCollisionBounds).
    bool Collider(int prop, int cdTypes, PropCollider* out) const;

    // getposstan: `position`'s tile with a circle of `radius` tested against
    // objects, doors, players, characters and path blockers. On success `*tile`
    // is where the test ended.
    static bool GetPosStan(Stan& stan, const Vec3& position, int tile, float radius, int* tileOut);
    // set_color_shading_from_tile for a prop standing on `tile`; clear when
    // there is no tile.
    ShadeColour TileShading(int tile) const;

    // chrGetChrWidthHeight.
    static void WidthHeight(const Chr& chr, float* width, float* height, float* always20);

    // sub_GAME_7F01FC10: validates the chr's step from `from` to `to` (which it
    // adjusts, and which may be the same position) and sets its ground height,
    // in `*groundOut`.
    void UpdateGround(int chrSlot, const Vec3* from, Vec3* to, float* groundOut, const ChrTiming& timing);

    // The frame accounting the model's root-move callback reads (the
    // globals sub_GAME_7F01FC10 uses).
    void SetTiming(const ChrTiming& timing) {
        timing_ = timing;
    }
    const ChrTiming& Timing() const {
        return timing_;
    }

    // The hat placements, or null.
    const Game::ChrHats* Hats() const {
        return hats_.get();
    }
    // The body and head table.
    const Game::ChrRecords* Records() const {
        return records_.get();
    }
    // HEAD_START and HEAD_F_START as slots of the body and head table (the
    // male heads lie between them), -1 for a table lacking them.
    int HeadStart() const {
        return headStart_;
    }
    int FemaleHeadStart() const {
        return femaleHeadStart_;
    }

    // Counts a character drawn this frame (g_playerPerm->
    // time_other_players_on_screen), which the stage folds into its stats.
    void CountDrawn() {
        drawn_++;
    }
    int TakeDrawnCount() {
        const int drawn = drawn_;
        drawn_ = 0;
        return drawn;
    }

    // sub_GAME_7F01F614: checks a step for a chr from `from` to `to`, sliding
    // `to` along the edge it hits when it can. Returns the tile the step ends
    // on, or kNoTile when it is blocked.
    int TestMove(int chrSlot, int stanTile, const Vec3* from, Vec3* to, bool updateLastMoveOk, const ChrTiming& timing);

    // chrDetectRooms.
    void DetectRooms(int chrSlot);

  private:
    float healthMod_ = 1.0f;
    // bodyChooseHead.
    int ChooseHead(int body, const Random& random);
    // get_current_random_body.
    int CurrentRandomBody() const;
    // Resolves the three name tables to slots of the body and head table.
    void ResolveTables();
    // makeonebody: the model for `body` and `head` (-1 for none), scaled by the
    // body's record. Null, after logging why, when a model does not load.
    std::shared_ptr<ModelHandle> MakeOneBody(int body, int head, bool sunglasses);
    // init_GUARDdata_with_set_values: -1 when there is no free slot.
    int InitChr(int prop, const Vec3& position, int tile, float angle, const ChrTiming& timing,
                std::shared_ptr<ModelHandle> model, float boundingRadius);

    Stan* stan_ = nullptr;
    PropList* props_ = nullptr;
    std::shared_ptr<const Game::ChrRecords> records_;
    std::shared_ptr<const Game::ChrHats> hats_;
    ModelFactory* models_ = nullptr;
    std::vector<Chr> chrs_;
    // The index in chrs_ of the character each propdef record spawned, -1 for none.
    std::vector<int> byDef_;
    int nextChrnum_ = kFirstAnonymousChrnum;
    // current_random_body, current_random_male_head and
    // current_random_female_head.
    uint32_t randomBody_ = 0;
    uint32_t randomMaleHead_ = 0;
    uint32_t randomFemaleHead_ = 0;
    // list_of_bodies, random_male_heads and random_female_heads as slots of
    // the body and head table; -1 for a name the table lacks.
    std::vector<int> bodies_;
    std::vector<int> maleHeads_;
    std::vector<int> femaleHeads_;
    int headStart_ = -1;
    int femaleHeadStart_ = -1;
    // animation_rate: init_guards starts it at 1.
    float animationRate_ = 1.0f;
    ChrTiming timing_;
    int drawn_ = 0;
    const AiLists* aiLists_ = nullptr;
    std::vector<Chr> background_;
};

} // namespace Frigate::Level
