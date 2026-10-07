#pragma once

#include <array>
#include <cstdint>

#include "level/StageSetup.h"

namespace Frigate::Level {

// PROP_* values (indices of the prop model table) that name the weapon models
// a multiplayer weapon set puts on the floor.
namespace MpProp {
constexpr int32_t ChrKalash = 184;
constexpr int32_t ChrM16 = 188;
constexpr int32_t ChrMp5k = 189;
constexpr int32_t ChrRuger = 190;
constexpr int32_t ChrWppk = 191;
constexpr int32_t ChrSkorpion = 193;
constexpr int32_t ChrUzi = 195;
constexpr int32_t ChrGrenade = 196;
constexpr int32_t ChrFnp90 = 197;
constexpr int32_t ChrRemoteMine = 199;
constexpr int32_t ChrProximityMine = 200;
constexpr int32_t ChrTimedMine = 201;
constexpr int32_t ChrWppkSil = 204;
constexpr int32_t ChrTt33 = 205;
constexpr int32_t ChrAutoShot = 207;
constexpr int32_t ChrGolden = 208;
constexpr int32_t ChrThrowKnife = 209;
constexpr int32_t ChrSniperRifle = 210;
constexpr int32_t ChrGrenadeLaunch = 185;
constexpr int32_t ChrLaser = 187;
constexpr int32_t ChrRocketLaunch = 211;
// PROP_FLAG: the Flag Tag scenario's flag.
constexpr int32_t Flag = 333;
} // namespace MpProp

// One row of s_mp_weapon_set (the US layout): what a weapon slot of a
// multiplayer stage's setup becomes.
struct MpWeaponSetEntry {
    int32_t item = 0;
    int32_t prop = 0;
    // Multiplied by 256 for the record's extrascale.
    float size = 1.0f;
    // AMMOTYPE of the ammo crate that follows the weapon, 0 for none.
    int32_t ammoType = 0;
    int32_t ammoAmount = 0;
    // 0 leaves the weapon out of the stage (a thrown weapon is only ever in the
    // crate's ammunition).
    bool allowPickup = true;
};

constexpr size_t kMpWeaponSetCount = 14;
constexpr size_t kMpWeaponSetSlots = 8;

using MpWeaponSet = std::array<MpWeaponSetEntry, kMpWeaponSetSlots>;

// getPtrMPWeaponSetData for the selected set (mp_weapon_set_text_table's
// order: slappers, pistols, throwing knives, automatics, power weapons,
// sniper rifles, grenades, remote mines, grenade launchers, timed mines,
// proximity mines, rockets, lasers, golden gun). An index outside the table
// reads the default set (rockets).
const MpWeaponSet& MpWeaponSetData(int set);

// What proplvreset2 and weaponAssignToHome do to a multiplayer stage's weapon
// and ammo crate records, in the order the setup lists them.
class MpWeaponStocking {
  public:
    // `set` is the selected weapon set; `tokenScenario` is Flag Tag
    // (SCENARIO_TLD), the only game the token is picked up in.
    MpWeaponStocking(int set, bool tokenScenario) : set_(set), tokenScenario_(tokenScenario) {
    }

    // weaponAssignToHome for a collectable no character holds: a slot of the
    // 0xF0 range becomes the set's weapon, and the token stays only in Flag
    // Tag. Returns whether the record becomes an object.
    bool StockWeapon(PropDef* def);
    // proplvreset2's PROPDEF_AMMO case: the slot of the crate that holds the
    // set's ammunition gets the set's amount, taken from the weapon placed
    // last. Returns whether the crate is made (it is not when the amount is 0).
    bool StockAmmoCrate(PropDef* def);

    // lastmpweaponnum: the set slot of the weapon placed last, -1 when that
    // weapon was not a set slot.
    int LastSlot() const {
        return lastSlot_;
    }

  private:
    int set_;
    bool tokenScenario_;
    int lastSlot_ = -1;
};

} // namespace Frigate::Level
