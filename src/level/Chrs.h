#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/ChrRecords.h"
#include "level/PropList.h"
#include "level/Stan.h"
#include "level/StanProps.h"
#include "level/StageSetup.h"

namespace Frigate::Level {

// CHRFLAG_*.
namespace ChrFlag {
constexpr uint32_t Init = 0x00000001;
constexpr uint32_t Clone = 0x00000002;
constexpr uint32_t Invincible = 0x00000010;
// CHRFLAG_LOCK_Y_POS: the chr's height is not taken from the tile it stands on.
constexpr uint32_t LockYPos = 0x00001000;
constexpr uint32_t Hidden = 0x00000400;
// CHRFLAG_00010000: the decomp does not name it; chrUpdateCollisionBounds
// gives a character carrying it no footprint.
constexpr uint32_t Unnamed0x10000 = 0x00010000;
} // namespace ChrFlag

// CHRHIDDEN_*.
namespace ChrHidden {
constexpr uint32_t None = 0;
constexpr uint32_t Moving = 0x0100;
} // namespace ChrHidden

// ACT_TYPE, the values chrs.actiontype takes that the level code so far reads.
namespace ChrAction {
constexpr int Init = 0;
constexpr int Die = 4;
constexpr int Dead = 5;
} // namespace ChrAction

// GUARD_SETUP_FLAG_*: GuardRecord::bitflags.
namespace GuardSetupFlag {
constexpr uint16_t Sunglasses = 0x0001;
constexpr uint16_t RandomSunglasses = 0x0002;
constexpr uint16_t Clone = 0x0004;
constexpr uint16_t Invincible = 0x0008;
} // namespace GuardSetupFlag

// The frame accounting a spawn reads (g_GlobalTimer, g_GlobalTimerDelta and
// g_ClockTimer).
struct ChrTiming {
    int globalTimer = 0;
    float globalTimerDelta = 1.0f;
    int clockTimer = 1;
};

// ChrRecord: a slot of g_ChrSlots. Only the fields the spawn sets and the
// prop collision tests read are here.
// TODO(https://github.com/liz-baker/frigate/issues/166): the action state
// (act_*), aim, beam, firing and weapon-holding fields, which
// init_GUARDdata_with_set_values zeroes and the AI first reads.
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
    // The propdef record's AI list number; ailistFindById resolves it.
    // TODO(https://github.com/liz-baker/frigate/issues/166): the AI list
    // rebasing, and the ailist, aioffset and aireturnlist fields.
    int aiListId = 0;
    int32_t aiReturnList = -1;
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
};

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
    void Reset(Stan* stan, PropList* props, std::shared_ptr<const Game::ChrRecords> records);

    // alloc_init_GUARDdata_entries: `count` guards plus ten spare slots.
    void AllocateSlots(int count);

    // bodiesReset: draws the stage's random head and body choices.
    void BodiesReset(const Random& random);

    // expand_09_characters for propdef record `def` of `setup`. False when
    // the guard's pad has no room for it, or it has nothing to spawn from.
    bool Spawn(const StageSetup& setup, size_t def, const ChrTiming& timing, const Random& random);
    // The PROPDEF_GUARD_ATTRIBUTE record's effect on the character it names.
    void ApplyAttribute(const PropDef& def);

    // chrFindByLiteralId.
    const Chr* FindByLiteralId(int chrnum) const;
    // The character spawned from propdef record `def`, or null.
    const Chr* ForDef(size_t def) const;
    const std::vector<Chr>& Slots() const {
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

    // chrGetChrWidthHeight.
    static void WidthHeight(const Chr& chr, float* width, float* height, float* always20);

    // sub_GAME_7F01FC10: validates the chr's step from `from` to `to` (which it
    // adjusts, and which may be the same position) and sets its ground height.
    // TODO(https://github.com/liz-baker/frigate/issues/166): the ACT_DIE
    // extra speed, which only a dying chr reaches.
    void UpdateGround(int chrSlot, const Vec3* from, Vec3* to, const ChrTiming& timing);

    // sub_GAME_7F01F614: checks a step for a chr from `from` to `to`, sliding
    // `to` along the edge it hits when it can. Returns the tile the step ends
    // on, or kNoTile when it is blocked.
    int TestMove(int chrSlot, int stanTile, const Vec3* from, Vec3* to, bool updateLastMoveOk, const ChrTiming& timing);

    // chrDetectRooms.
    void DetectRooms(int chrSlot);

  private:
    // bodyChooseHead.
    int ChooseHead(int body, const Random& random);
    // get_current_random_body.
    int CurrentRandomBody() const;
    // Resolves the three name tables to slots of the body and head table.
    void ResolveTables();
    // init_GUARDdata_with_set_values: -1 when there is no free slot.
    int InitChr(int prop, const Vec3& position, int tile, float angle, const ChrTiming& timing);

    Stan* stan_ = nullptr;
    PropList* props_ = nullptr;
    std::shared_ptr<const Game::ChrRecords> records_;
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
};

} // namespace Frigate::Level
