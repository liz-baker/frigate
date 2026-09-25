#include <cstdio>
#include <string>

#include "level/PlayerWeapons.h"
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

// Item 0 is the unarmed slot, 1 the fist, 2 a pistol, 3 a gun that stops
// crouching, 4 a gun whose ammo goes straight into the magazine.
const char* kTable = "item\t0\tITEM_UNARMED\t0\t0\t0\t0x110\n"
                     "item\t1\tITEM_FIST\t1\t0\t0\t0x64d91\n"
                     "item\t2\tITEM_PISTOL\t1\t1\t7\t0x170fb9\n"
                     "item\t3\tITEM_SCOPE\t1\t3\t8\t0x178a70\n"
                     "item\t4\tITEM_CLIPPED\t1\t4\t5\t0x370af9\n"
                     "ammo\t0\tAMMO_NONE\t0\n"
                     "ammo\t1\tAMMO_9MM\t800\n"
                     "ammo\t2\tAMMO_9MM_2\t200\n"
                     "ammo\t3\tAMMO_RIFLE\t400\n"
                     "ammo\t4\tAMMO_SHOTGUN\t100\n";

struct Scene {
    Inventory inventory;
    PlayerWeapons weapons;

    Scene() {
        inventory.AddItem(kItemFist);
        inventory.AddItem(2);
        inventory.AddItem(3);
        weapons.Init(WeaponTable::Parse(kTable), &inventory);
    }

    void Tick(int times) {
        for (int i = 0; i < times; i++) {
            weapons.Tick(1);
        }
    }
};

void TestTableParses() {
    const auto table = WeaponTable::Parse(kTable);
    Check(table != nullptr, "a well-formed table parses");
    Check(table->AmmoType(2) == 1 && table->Item(2).magSize == 7, "an item's ammo type and magazine");
    Check(table->HasFlag(3, WeaponFlag::DisableCrouch) && !table->HasFlag(2, WeaponFlag::DisableCrouch),
          "flags are per item");
    Check(table->MaxAmmo(3) == 400 && table->MaxAmmo(99) == 0, "ammo limits by type");
    Check(WeaponTable::Parse("item\t1\tITEM_X\t0\t0\t0\t0x0\n") == nullptr, "an item out of order is malformed");
    Check(WeaponTable::Parse("nonsense\n") == nullptr, "an unknown line is malformed");
}

void TestGiveAmmo() {
    Scene scene;
    scene.weapons.GiveAmmo(1, 5000);
    Check(scene.weapons.AmmoHeld(1) == 800, "ammo is capped at the type's limit");
    scene.weapons.GiveAmmo(1, 30);
    Check(scene.weapons.AmmoHeld(1) == 30, "a grant sets the amount held");

    scene.weapons.EquipWeapon(HandRight, 4);
    scene.Tick(60);
    Check(scene.weapons.WeaponId(HandRight) == 4, "the clip-limited gun is in hand");
    scene.weapons.GiveAmmo(4, 3);
    Check(scene.weapons.Hands(HandRight).ammoInMagazine == 3 && scene.weapons.AmmoHeld(4) == 0,
          "ammo for a clip-limited gun goes into its magazine");
    scene.weapons.GiveAmmo(4, 50);
    Check(scene.weapons.Hands(HandRight).ammoInMagazine == 5, "and stops at the magazine size");
}

void TestEquipRunsThroughTheSwitchStates() {
    Scene scene;
    scene.weapons.EquipWeapon(HandLeft, kItemUnarmed);
    scene.weapons.EquipWeapon(HandRight, 2);
    Check(scene.weapons.WeaponId(HandRight) == kItemUnarmed, "nothing is equipped until the hand ticks");

    scene.Tick(1);
    Check(scene.weapons.Hands(HandRight).actionState == GunState::SwitchLower, "the hand lowers");
    scene.Tick(15);
    Check(scene.weapons.WeaponId(HandRight) == kItemUnarmed, "the old weapon is held while lowering");
    scene.Tick(1);
    Check(scene.weapons.WeaponId(HandRight) == 2, "the swap puts the new weapon in hand");
    Check(scene.weapons.EquippedIndex() == 1, "and finds it in the inventory");
    scene.Tick(1);
    Check(scene.weapons.HandItem(HandRight) == 2, "the hand's model is the new weapon's");
    Check(scene.weapons.Hands(HandRight).actionState == GunState::SwitchRaise, "the hand raises");
    scene.Tick(40);
    Check(scene.weapons.Hands(HandRight).actionState == GunState::Idle, "and settles idle");
    Check(scene.weapons.WeaponId(HandLeft) == kItemUnarmed, "the left hand stays empty");
}

void TestCrouchFlagFollowsTheWeapon() {
    Scene scene;
    Check(!scene.weapons.RightHandHasFlag(WeaponFlag::DisableCrouch), "unarmed can crouch");
    scene.weapons.EquipWeapon(HandRight, 3);
    scene.Tick(60);
    Check(scene.weapons.RightHandHasFlag(WeaponFlag::DisableCrouch), "a scoped gun does not");
}

void TestUnequipReturnsTheMagazine() {
    Scene scene;
    scene.weapons.EquipWeapon(HandRight, 4);
    scene.Tick(60);
    scene.weapons.GiveAmmo(4, 4);
    scene.weapons.EquipWeapon(HandRight, 2);
    scene.Tick(60);
    Check(scene.weapons.AmmoHeld(4) == 4, "changing weapon puts the magazine back in the ammo held");
}

} // namespace

int main() {
    TestTableParses();
    TestGiveAmmo();
    TestEquipRunsThroughTheSwitchStates();
    TestCrouchFlagFollowsTheWeapon();
    TestUnequipReturnsTheMagazine();
    return gFailures == 0 ? 0 : 1;
}
