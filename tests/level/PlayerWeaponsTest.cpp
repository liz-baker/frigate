#include <cmath>
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
const char* kTable =
    "item\t0\tITEM_UNARMED\t0\t0\t0\t0x110\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "item\t1\tITEM_FIST\t1\t0\t0\t0x64d91\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "item\t2\tITEM_PISTOL\t1\t1\t7\t0x170fb9\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "item\t3\tITEM_SCOPE\t1\t3\t8\t0x178a70\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "item\t4\tITEM_CLIPPED\t1\t4\t5\t0x370af9\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
    "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n"
    "ammo\t1\tAMMO_9MM\t800\t0x0\t0.0\n"
    "ammo\t2\tAMMO_9MM_2\t200\t0x0\t0.0\n"
    "ammo\t3\tAMMO_RIFLE\t400\t0x0\t0.0\n"
    "ammo\t4\tAMMO_SHOTGUN\t100\t0x0\t0.0\n";

struct Scene {
    Inventory inventory;
    PlayerWeapons weapons;

    Scene() {
        inventory.AddItem(kItemFist);
        inventory.AddItem(2);
        inventory.AddItem(3);
        weapons.Init(WeaponTable::Parse(kTable), &inventory, [] { return 0u; });
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
    Check(WeaponTable::Parse(
              "item\t1\tITEM_X\t0\t0\t0\t0x0\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n") ==
              nullptr,
          "an item out of order is malformed");
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

bool Near(float a, float b, float epsilon = 0.0005f) {
    return std::fabs(a - b) < epsilon;
}

void TestChangeTiltsTheGun() {
    Scene scene;
    scene.weapons.EquipWeapon(HandRight, 2);
    scene.Tick(9);
    const HandState& hand = scene.weapons.Hands(HandRight);
    Check(hand.actionState == GunState::SwitchLower && hand.hasTransform, "lowering offsets the gun");
    // 8 ticks into a 16 tick lowering, half of M_LN2F.
    const float angle = 8.0f * 0.69813174f / 16.0f;
    Check(Near(hand.transform.m[1][1], std::cos(angle)) && Near(hand.transform.m[1][2], std::sin(angle)),
          "the lowering tilts the gun about X");
    Check(Near(hand.transform.m[3][1], (1.0f - std::cos(angle)) * -60.0f) &&
              Near(hand.transform.m[3][2], std::sin(angle) * 15.0f),
          "and pulls it down and forward");

    scene.Tick(9);
    Check(scene.weapons.Hands(HandRight).hasTransform &&
              Near(scene.weapons.Hands(HandRight).transform.m[1][1], std::cos(0.69813174f)),
          "the swap holds the gun at the full tilt");
    scene.Tick(60);
    Check(scene.weapons.Hands(HandRight).actionState == GunState::Idle && !scene.weapons.Hands(HandRight).hasTransform,
          "an idle hand has no offset");
}

void TestRaiseFillsTheMagazine() {
    Scene scene;
    scene.weapons.GiveAmmo(1, 30);
    scene.weapons.EquipWeapon(HandRight, 2);
    scene.Tick(60);
    Check(scene.weapons.Hands(HandRight).ammoInMagazine == 7, "raising a weapon fills its magazine");
    Check(scene.weapons.AmmoHeld(1) == 23, "from the ammo held");
}

// Every random draw is one half.
uint32_t Half() {
    return 0x80000000u;
}

void TestSwayBlendsAreDrawnAtInit() {
    Inventory inventory;
    PlayerWeapons weapons;
    weapons.Init(WeaponTable::Parse(kTable), &inventory, Half);
    const HandState& hand = weapons.Hands(HandRight);
    Check(hand.curBlendPos == 3, "three blends leave the hand on the fourth slot");
    Check(Near(hand.blendPos[2][0], 1.875f) && Near(hand.blendPos[2][1], 2.5f) && Near(hand.blendPos[2][2], 0.0f),
          "the first blend fills the slot two ahead");
    Check(Near(hand.blendPos[3][1], -2.5f), "the height alternates in sign");
    Check(Near(hand.blendPos[0][0], -1.875f), "and the third blend is on the other side");
    Check(hand.sideFlag == -2 && Near(hand.blendScale1, -1.0f), "the side and scale flags step on");
    Check(Near(hand.blendLook[2][2], -1.0f) && Near(hand.blendUp[2][1], 1.0f), "look and up keep their axes");
}

void TestSwayFollowsSpeedAndTurn() {
    Inventory inventory;
    PlayerWeapons weapons;
    weapons.Init(WeaponTable::Parse(kTable), &inventory, Half);
    weapons.SetBondWeaponSway(0.0f, 0.0f, 1.0f, 0.5f, 0.25f, 1, 1.0f, Half);
    Check(Near(weapons.SwayAmplitude(), 0.050000012f), "one tick at full speed smooths the amplitude to a twentieth");
    Check(Near(weapons.Hands(HandRight).thetaDisplacement, -1.75f * 0.25f) &&
              Near(weapons.Hands(HandLeft).vertaDisplacement, -2.0f * 0.5f),
          "the turn and look speeds displace both hands");

    weapons.SetOffsetRelated(0.0f);
    Check(Near(weapons.Hands(HandRight).offset2[2], 0.0f), "level, the gun is not pushed forward");
    weapons.SetOffsetRelated(3.14159265f);
    Check(Near(weapons.Hands(HandLeft).offset2[2], 10.0f), "looking straight down pushes it forward ten units");
}

void TestAimFollowsTheTurn() {
    Inventory inventory;
    PlayerWeapons weapons;
    weapons.Init(WeaponTable::Parse(kTable), &inventory, Half);
    GunView view;
    view.scaleX = 0.003f;
    view.scaleY = 0.003f;
    weapons.AimSwivel(0.0f, 0.0f, 1, view);
    Check(Near(weapons.Aim().aimScreenX, 160.0f) && Near(weapons.Aim().aimScreenY, 120.0f),
          "with no turn the gun aims at the middle of the screen");
    Check(Near(weapons.Hands(HandRight).aim[2], -1000.0f), "a thousand units ahead");

    weapons.AimSwivel(1.0f, 0.0f, 1, view);
    // The crosshair damping steps to 0.86 on the first call, the aim's stays 0.9.
    Check(Near(weapons.Aim().crosshairScreenX, (1.0f * (1.0f - 0.86f) * 160.0f) + 160.0f), "the crosshair leads");
    Check(Near(weapons.Aim().aimScreenX, (1.0f * (1.0f - 0.9f) * 160.0f) + 160.0f), "and the aim follows behind");
    Check(weapons.Hands(HandRight).aim[0] > 0.0f, "the gun turns toward the crosshair");
}

void TestTheWatchMenuTakesTheHandsItem() {
    Scene scene;
    scene.weapons.EquipWeapon(HandRight, 2);
    scene.Tick(60);
    Check(scene.weapons.WeaponId(HandRight) == 2, "the pistol is in hand");

    scene.weapons.DrawItemInHand(HandRight, 3);
    scene.Tick(80);
    Check(scene.weapons.Hands(HandRight).weaponNumWatchMenu == 3, "the watch menu's item is drawn in the hand");
    Check(scene.weapons.Hands(HandRight).actionState == GunState::Idle, "and the hand settles idle");

    scene.weapons.RestoreFromWatchMenu(HandRight);
    Check(scene.weapons.Hands(HandRight).weaponNumWatchMenu == -1, "leaving the menu clears the substitution");
    Check(scene.weapons.WeaponId(HandRight) == 2, "and the pistol is still the weapon held");
}

void TestAmmoIcons() {
    const auto table = WeaponTable::Parse(
        "item\t0\tITEM_UNARMED\t0\t0\t0\t0x110\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\n"
        "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n"
        "ammo\t1\tAMMO_9MM\t800\t0xc84\t-2.0\n");
    Check(table != nullptr, "a table with ammo icons parses");
    if (table == nullptr) {
        return;
    }
    Check(table->Icon(1).image == 0xC84 && table->Icon(1).yOffset == -2.0f, "an ammo type has its icon and offset");
    Check(table->Icon(0).image == 0 && table->Icon(7).image == 0, "no ammo and an unknown type have none");
}

} // namespace

int main() {
    TestTheWatchMenuTakesTheHandsItem();
    TestAmmoIcons();
    TestChangeTiltsTheGun();
    TestRaiseFillsTheMagazine();
    TestSwayBlendsAreDrawnAtInit();
    TestSwayFollowsSpeedAndTurn();
    TestAimFollowsTheTurn();
    TestTableParses();
    TestGiveAmmo();
    TestEquipRunsThroughTheSwitchStates();
    TestCrouchFlagFollowsTheWeapon();
    TestUnequipReturnsTheMagazine();
    return gFailures == 0 ? 0 : 1;
}
