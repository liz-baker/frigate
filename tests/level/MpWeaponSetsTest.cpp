#include <cstdio>
#include <set>

#include "level/MpWeaponSets.h"
#include "level/PickupText.h"
#include "level/WeaponTable.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr int kSlappers = 0;
constexpr int kPistols = 1;
constexpr int kKnives = 2;
constexpr int kAutomatics = 3;
constexpr int kGrenades = 6;
constexpr int kRemoteMines = 7;
constexpr int kTimedMines = 9;
constexpr int kProximityMines = 10;
constexpr int kRockets = 11;
constexpr int kLasers = 12;
constexpr int kGolden = 13;

// The set slots a setup record names: ITEM_UNARMED + 0xF0 and the seven items
// after it.
PropDef SlotRecord(int slot) {
    PropDef def;
    def.type = static_cast<uint8_t>(PropDefType::Collectable);
    def.weaponNum = 0xF0 + slot;
    def.model = 0x10;
    def.extraScale = 0x100;
    return def;
}

PropDef AmmoCrate() {
    PropDef def;
    def.type = static_cast<uint8_t>(PropDefType::Ammo);
    for (auto& slot : def.ammoSlots) {
        slot.quantity = 99;
    }
    return def;
}

void TestTables() {
    Check(kMpWeaponSetCount == 14 && kMpWeaponSetSlots == 8, "14 sets of 8 slots");
    for (size_t set = 0; set < kMpWeaponSetCount; set++) {
        for (const MpWeaponSetEntry& entry : MpWeaponSetData(static_cast<int>(set))) {
            Check(entry.size >= 1.0f && entry.size <= 3.0f, "every size is between 1 and 3");
            Check(entry.ammoAmount >= 0 && entry.ammoType >= 0 && entry.ammoType <= 13, "an ammo type is a global one");
        }
        // The two slots of a pair are the same weapon.
        const MpWeaponSet& rows = MpWeaponSetData(static_cast<int>(set));
        if (set != static_cast<size_t>(kGolden) && set != static_cast<size_t>(kPistols)) {
            for (size_t slot = 0; slot < kMpWeaponSetSlots; slot += 2) {
                Check(rows[slot].item == rows[slot + 1].item && rows[slot].prop == rows[slot + 1].prop,
                      "a weapon takes two slots");
            }
        }
    }

    const MpWeaponSet& slappers = MpWeaponSetData(kSlappers);
    for (const MpWeaponSetEntry& entry : slappers) {
        Check(entry.item == kItemUnarmed && entry.allowPickup && entry.ammoAmount == 0,
              "the slappers set is nothing to pick up");
    }

    const MpWeaponSet& pistols = MpWeaponSetData(kPistols);
    Check(pistols[0].item == kItemDd44 && pistols[2].item == kItemDd44 && pistols[3].item == kItemPp7Silenced &&
              pistols[5].item == kItemPp7Silenced && pistols[6].item == kItemRuger && pistols[7].item == kItemRuger,
          "the pistols are a DD44, a silenced PP7 and a Cougar Magnum");
    Check(pistols[0].prop == MpProp::ChrTt33 && pistols[3].prop == MpProp::ChrWppkSil &&
              pistols[6].prop == MpProp::ChrRuger && pistols[6].ammoType == AmmoKind::Magnum &&
              pistols[6].ammoAmount == 0x32,
          "with their models and ammunition");

    for (const MpWeaponSetEntry& entry : MpWeaponSetData(kKnives)) {
        Check(entry.item == kItemThrowKnife && !entry.allowPickup && entry.ammoType == AmmoKind::Knife &&
                  entry.ammoAmount == 10,
              "throwing knives are only in the crates");
    }

    const MpWeaponSet& automatics = MpWeaponSetData(kAutomatics);
    Check(automatics[4].item == kItemKlobb && automatics[4].size == 1.5f && automatics[6].item == kItemD5k &&
              automatics[6].ammoAmount == 0x64,
          "the automatics end with a Klobb at 1.5 and a D5K");

    struct Thrown {
        int set;
        int32_t item;
        int32_t prop;
        int32_t ammoType;
    };
    for (const Thrown& thrown :
         { Thrown{ kGrenades, kItemGrenade, MpProp::ChrGrenade, AmmoKind::Grenade },
           Thrown{ kRemoteMines, kItemRemoteMine, MpProp::ChrRemoteMine, AmmoKind::RemoteMine },
           Thrown{ kTimedMines, kItemTimedMine, MpProp::ChrTimedMine, AmmoKind::TimedMine },
           Thrown{ kProximityMines, kItemProximityMine, MpProp::ChrProximityMine, AmmoKind::ProximityMine } }) {
        const MpWeaponSetEntry& last = MpWeaponSetData(thrown.set)[7];
        Check(last.item == thrown.item && last.prop == thrown.prop && last.ammoType == thrown.ammoType &&
                  last.ammoAmount == 5 && !last.allowPickup && last.size == 1.5f,
              "a set of thrown weapons ends with five of them, only in the crates");
    }
    Check(MpWeaponSetData(kRemoteMines)[4].prop == MpProp::ChrKalash &&
              MpWeaponSetData(kTimedMines)[4].prop == MpProp::ChrKalash &&
              MpWeaponSetData(kProximityMines)[4].prop == MpProp::ChrM16,
          "the AR33 of the remote and timed mine sets has the KF7's model, the proximity set's its own");

    const MpWeaponSetEntry& rockets = MpWeaponSetData(kRockets)[7];
    Check(rockets.item == kItemRocketLauncher && rockets.ammoType == AmmoKind::Rockets && rockets.ammoAmount == 6 &&
              rockets.allowPickup,
          "the rocket launcher is picked up, with six rockets");
    const MpWeaponSetEntry& lasers = MpWeaponSetData(kLasers)[7];
    Check(lasers.item == kItemLaser && lasers.ammoType == 0 && lasers.ammoAmount == 0 && lasers.allowPickup,
          "the laser has no ammunition");
    const MpWeaponSet& golden = MpWeaponSetData(kGolden);
    Check(golden[6].item == kItemPp7Silenced && golden[7].item == kItemGoldenGun && golden[7].ammoType == 13 &&
              golden[7].ammoAmount == 10 && golden[7].prop == MpProp::ChrGolden,
          "the golden set has a single golden gun");

    Check(MpWeaponSetData(-1).data() == MpWeaponSetData(0xB).data() &&
              MpWeaponSetData(14).data() == MpWeaponSetData(0xB).data(),
          "a set outside the table is the default");
}

void TestStockWeapon() {
    MpWeaponStocking rockets(kRockets, false);
    PropDef rocketSlot = SlotRecord(7);
    Check(rockets.StockWeapon(&rocketSlot) && rocketSlot.weaponNum == kItemRocketLauncher &&
              rocketSlot.model == MpProp::ChrRocketLaunch && rocketSlot.extraScale == 384 && rockets.LastSlot() == 7,
          "a set slot becomes the set's weapon, model and size");

    PropDef pistol = SlotRecord(0);
    Check(rockets.StockWeapon(&pistol) && pistol.weaponNum == kItemDd44 && pistol.extraScale == 768 &&
              rockets.LastSlot() == 0,
          "the first slot is the first weapon at its size");

    MpWeaponStocking knives(kKnives, false);
    PropDef knife = SlotRecord(3);
    Check(!knives.StockWeapon(&knife) && knife.weaponNum == kItemThrowKnife,
          "a weapon only the crates give is not made");

    MpWeaponStocking slappers(kSlappers, false);
    PropDef slap = SlotRecord(5);
    Check(!slappers.StockWeapon(&slap), "nor is anything of the slappers set");

    PropDef real;
    real.type = static_cast<uint8_t>(PropDefType::Collectable);
    real.weaponNum = kItemKlobb;
    real.model = 0x22;
    real.extraScale = 0x100;
    Check(rockets.StockWeapon(&real) && real.weaponNum == kItemKlobb && real.model == 0x22 &&
              real.extraScale == 0x100 && rockets.LastSlot() == -1,
          "any other weapon is left as it is, and is the last one placed");

    PropDef token = real;
    token.weaponNum = kItemToken;
    Check(!rockets.StockWeapon(&token), "the token is left out of every game but Flag Tag");
    MpWeaponStocking flagTag(kRockets, true);
    Check(flagTag.StockWeapon(&token) && token.weaponNum == kItemToken, "and made in Flag Tag");
}

void TestStockAmmoCrate() {
    MpWeaponStocking rockets(kRockets, false);
    PropDef weapon = SlotRecord(7);
    rockets.StockWeapon(&weapon);
    PropDef crate = AmmoCrate();
    Check(rockets.StockAmmoCrate(&crate) && crate.ammoSlots[AmmoKind::Rockets - 1].quantity == 6 &&
              crate.ammoSlots[0].quantity == 99,
          "the crate after a weapon holds six rockets, and the other slots are as the file has them");

    MpWeaponStocking knives(kKnives, false);
    PropDef knife = SlotRecord(2);
    knives.StockWeapon(&knife);
    crate = AmmoCrate();
    Check(knives.StockAmmoCrate(&crate) && crate.ammoSlots[AmmoKind::Knife - 1].quantity == 10,
          "the crates are where the throwing knives come from");

    MpWeaponStocking lasers(kLasers, false);
    PropDef laser = SlotRecord(7);
    lasers.StockWeapon(&laser);
    crate = AmmoCrate();
    Check(!lasers.StockAmmoCrate(&crate), "the laser has no crate");

    MpWeaponStocking slappers(kSlappers, false);
    PropDef slap = SlotRecord(1);
    slappers.StockWeapon(&slap);
    crate = AmmoCrate();
    Check(!slappers.StockAmmoCrate(&crate) && crate.ammoSlots[0].quantity == 0, "nor do the slappers");
}

} // namespace

int main() {
    TestTables();
    TestStockWeapon();
    TestStockAmmoCrate();
    if (gFailures == 0) {
        std::printf("MpWeaponSetsTest: all passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
