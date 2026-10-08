#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "level/Casings.h"
#include "level/GunKeyframes.h"
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

bool Near(float a, float b, float epsilon = 0.0005f) {
    return std::fabs(a - b) < epsilon;
}

constexpr uint32_t kShown = 0x800;
constexpr uint32_t kHasAmmo = 0x40000;
constexpr uint32_t kClicky = 0x10;

// One `item` line of the weapon table: the placement is the same for every
// item, the firing columns are the arguments.
std::string Item(int index, int ammoType, int magazine, uint32_t flags, int autoRate, int singleRate,
                 std::array<int, 4> recoil, const char* cartridge = "-") {
    char flagText[16];
    std::snprintf(flagText, sizeof flagText, "0x%x", flags);
    return "item\t" + std::to_string(index) + "\tITEM\t1\t" + std::to_string(ammoType) + "\t" +
           std::to_string(magazine) + "\t" + flagText +
           "\t-\t-\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0.0\t" + std::to_string(autoRate) + "\t" +
           std::to_string(singleRate) + "\t" + std::to_string(recoil[0]) + "\t" + std::to_string(recoil[1]) + "\t" +
           std::to_string(recoil[2]) + "\t" + std::to_string(recoil[3]) + "\t2.0\t4.0\t" + cartridge +
           "\t0.0\t10.0\t4.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t0\t0\n";
}

// Item 0 is the unarmed slot, 1 the fist, 2 the knife, 4 the PP7 (a pistol
// with a casing), 5 a gun that clicks on an empty magazine, 8 the KF7 (an
// automatic that fires every third tick) and 9 a burst weapon.
std::string Table() {
    std::string text;
    for (int i = 0; i < 12; i++) {
        switch (i) {
            case 2:
                text += Item(i, 0, 0, kShown | kClicky, -1, 0, { 4, 8, 0, 6 });
                break;
            case 4:
                text += Item(i, 1, 7, kShown | kHasAmmo, -1, 0, { 3, 5, 0, -1 }, "models/gun/GcartridgeZ");
                break;
            case 5:
                text += Item(i, 1, 7, kShown | kHasAmmo | kClicky, -1, 0, { 3, 5, 0, -1 });
                break;
            case 8:
                text += Item(i, 2, 30, kShown | kHasAmmo, 3, 0, { 2, 4, 2, 3 }, "models/gun/GcartrifleZ");
                break;
            case 9:
                text += Item(i, 2, 30, kShown | kHasAmmo | 0x4, 3, 0, { 2, 4, 2, 3 });
                break;
            default:
                text += Item(i, 0, 0, i == 0 ? 0x110 : kShown, -1, 0, { 0, 0, 0, 0 });
                break;
        }
    }
    text += "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n";
    text += "ammo\t1\tAMMO_9MM\t800\t0x0\t0.0\n";
    text += "ammo\t2\tAMMO_RIFLE\t400\t0x0\t0.0\n";
    return text;
}

struct Scene {
    Inventory inventory;
    PlayerWeapons weapons;
    uint32_t draws = 0;
    // The rest of the game's state a tick reads: the pause and menu flags.
    GunTickInput game;

    Scene() {
        for (const int item : { 1, 2, 4, 5, 8, 9 }) {
            inventory.AddItem(item);
        }
        weapons.Init(WeaponTable::Parse(Table()), &inventory, [] { return 0u; });
    }

    // One frame of the gun tick.
    void Tick(bool trigger) {
        GunTickInput input = game;
        input.triggerOn = trigger;
        input.random = [this] {
            draws++;
            return 0x40000000u;
        };
        weapons.TickGameplay(input, nullptr);
    }
    void Tick(int times, bool trigger) {
        for (int i = 0; i < times; i++) {
            Tick(trigger);
        }
    }

    // Equips `item` in the right hand with full ammo and waits for it to raise.
    void Arm(int item, int ammo = 100) {
        weapons.GiveAmmo(weapons.Table().AmmoType(item), ammo);
        weapons.EquipWeapon(HandLeft, kItemUnarmed);
        weapons.EquipWeapon(HandRight, item);
        Tick(60, false);
    }
};

void TestPistolFiresOnceAndRecoils() {
    Scene scene;
    scene.Arm(4);
    const HandState& hand = scene.weapons.Hands(HandRight);
    Check(hand.actionState == GunState::Idle && hand.ammoInMagazine == 7, "the raised pistol is idle and loaded");

    scene.Tick(true);
    Check(hand.actionState == GunState::Fire && hand.firingStatus == 1 && hand.flashVisible,
          "pulling the trigger fires and shows the flash");
    Check(hand.ammoInMagazine == 6 && hand.volley == 1, "a shot takes a round");
    Check(scene.weapons.Firing(HandRight) && scene.weapons.Noise(HandRight) > 0.0f, "and makes noise");

    scene.Tick(true);
    Check(hand.actionState == GunState::Recoil1 && hand.firingStatus == 0 && !hand.flashVisible,
          "a held trigger does not fire a pistol again");
    Check(hand.ammoInMagazine == 6, "so no more rounds go");

    // The kick takes 3 ticks and the settle 5; the trigger is still held, so
    // the recoil waits for its release before going idle.
    scene.Tick(true);
    Check(hand.hasTransform && hand.recoilAngle > 3.14159f, "the gun is thrown back and up");
    scene.Tick(10, false);
    Check(hand.actionState == GunState::Idle && !hand.hasTransform, "the recoil ends idle");
    Check(Near(hand.recoilAngle, hand.recoilAngle) && hand.triggerReleased == 1, "with the trigger released");

    scene.Tick(true);
    Check(hand.actionState == GunState::Fire && hand.ammoInMagazine == 5, "the pistol fires again on a new press");
}

// field_87D: in a game of two or more a paused game or an open menu fires with
// no flash, and an open menu fires nothing; a single player's gun is never quiet.
void TestMenuQuietsTheFlash() {
    struct Mode {
        bool multiplayer;
        bool paused;
        bool menuOn;
        bool flash;
        bool fires;
        const char* what;
    };
    const Mode modes[] = {
        { true, false, false, true, true, "a game of several players flashes and fires" },
        { true, false, true, false, false, "an open menu neither flashes nor fires" },
        { true, true, false, false, true, "a paused game fires without a flash" },
        { true, true, true, false, false, "a paused game with a menu open does neither" },
        { false, true, false, true, true, "a single player's gun ignores the pause" },
        { false, false, true, true, false, "and shows its flash with a menu open, which fires nothing" },
    };
    for (const int item : { 4, 8 }) {
        for (const Mode& mode : modes) {
            Scene scene;
            scene.game.multiplayer = mode.multiplayer;
            scene.game.paused = mode.paused;
            scene.game.menuOn = mode.menuOn;
            scene.Arm(item);
            const HandState& hand = scene.weapons.Hands(HandRight);
            scene.Tick(true);
            std::string what = std::string(item == 4 ? "pistol: " : "automatic: ") + mode.what;
            Check(hand.flashVisible == mode.flash, (what + " (flash)").c_str());
            Check((hand.firingStatus != 0) == mode.fires, (what + " (shot)").c_str());
        }
    }
}

void TestAutomaticFiresEveryThirdTick() {
    Scene scene;
    scene.Arm(8);
    const HandState& hand = scene.weapons.Hands(HandRight);
    int shots = 0;
    for (int i = 0; i < 9; i++) {
        scene.Tick(true);
        shots += hand.firingStatus;
    }
    Check(shots == 3, "a held trigger fires the KF7 on every third tick");
    Check(hand.ammoInMagazine == 27 && hand.volley == 3, "and spends a round each");
    scene.Tick(false);
    Check(hand.actionState == GunState::Recoil1, "releasing the trigger starts the recoil");
}

void TestEmptyMagazineReloadsFromAmmoHeld() {
    Scene scene;
    scene.Arm(4, 14);
    const HandState& hand = scene.weapons.Hands(HandRight);
    Check(hand.ammoInMagazine == 7 && scene.weapons.AmmoHeld(1) == 7, "a pistol and a spare magazine");
    for (int i = 0; i < 7; i++) {
        scene.Tick(true);
        scene.Tick(10, false);
    }
    Check(hand.ammoInMagazine == 0 || hand.actionState != GunState::Idle, "seven shots empty it");

    scene.Tick(120, false);
    Check(hand.ammoInMagazine == 7 && scene.weapons.AmmoHeld(1) == 0, "the hand reloads on its own");
    Check(hand.actionState == GunState::Idle, "and goes idle");
}

void TestReloadStatesTiltTheGun() {
    Scene scene;
    scene.Arm(4, 14);
    const HandState& hand = scene.weapons.Hands(HandRight);
    scene.Tick(true);
    scene.Tick(10, false);
    Check(hand.ammoInMagazine == 6 && hand.actionState == GunState::Idle, "a shot leaves the magazine short");
    // The gun draw sets this once the hand's model is posed.
    scene.weapons.MutableHand(HandRight).modelShown = true;
    scene.weapons.AttemptReload(HandRight);
    scene.Tick(false);
    Check(hand.actionState == GunState::ReloadLower, "B asks for a reload");
    scene.Tick(8, false);
    Check(hand.hasTransform && hand.transform.m[1][1] < 1.0f, "the gun tilts down while it reloads");
    scene.Tick(80, false);
    Check(hand.actionState == GunState::Idle, "and the reload finishes");
}

void TestClickyGunDryFires() {
    Scene scene;
    scene.Arm(5, 0);
    const HandState& hand = scene.weapons.Hands(HandRight);
    Check(hand.ammoInMagazine == 0, "no ammo, no magazine");
    scene.Tick(true);
    Check(hand.actionState == GunState::TriggerPress || hand.actionState == GunState::Fire ||
              hand.actionState == GunState::DryFire,
          "a clicky gun still takes the trigger");
    scene.Tick(2, true);
    Check(hand.actionState == GunState::DryFire && hand.firingStatus == 0, "and clicks instead of firing");
    scene.Tick(30, false);
    Check(hand.actionState == GunState::Idle, "the click passes");
}

void TestDualWieldAlternatesOnFreshPresses() {
    Scene scene;
    scene.weapons.GiveAmmo(1, 100);
    scene.inventory.AddDoubles(4, 4);
    scene.weapons.EquipWeapon(HandRight, 4);
    scene.weapons.EquipWeapon(HandLeft, 4);
    scene.Tick(80, false);
    Check(scene.weapons.WeaponId(HandRight) == 4 && scene.weapons.WeaponId(HandLeft) == 4, "two pistols in hand");

    scene.Tick(true);
    const bool firstRight = scene.weapons.Firing(HandRight);
    Check(scene.weapons.Firing(HandRight) != scene.weapons.Firing(HandLeft), "one hand fires");
    scene.Tick(10, false);
    scene.Tick(true);
    Check(scene.weapons.Firing(HandRight) != firstRight, "the other hand fires on the next press");
}

void TestKnifeDrawsForItsSlash() {
    Scene scene;
    scene.Arm(2);
    scene.draws = 0;
    scene.Tick(true);
    const HandState& hand = scene.weapons.Hands(HandRight);
    Check(hand.actionState == GunState::KnifeSlash1Begin || hand.actionState == GunState::KnifeSlash2Begin,
          "the knife begins a slash");
    Check(scene.draws == 1, "choosing the slash draws a random number");
    scene.Tick(20, true);
    Check(hand.actionState == GunState::KnifeSlash1Strike || hand.actionState == GunState::KnifeSlash2Strike ||
              hand.actionState == GunState::KnifeSlash1Recover || hand.actionState == GunState::KnifeSlash2Recover,
          "the slash strikes");
    Check(scene.draws == 2, "and the swish draws another");
    scene.Tick(80, false);
    Check(hand.actionState == GunState::Idle, "the knife recovers");
}

void TestNoiseRisesWithShotsAndDecays() {
    Scene scene;
    scene.Arm(8);
    scene.Tick(9, true);
    const float loud = scene.weapons.Noise(HandRight);
    Check(loud > 0.0f && loud <= 10.0f, "shots raise the noise to the gun's limit");
    scene.Tick(120, false);
    Check(scene.weapons.Noise(HandRight) < loud, "which fades");
}

void TestKeyframesPlayToTheirEnd() {
    Mtxf matrix;
    Check(SampleKeyframes(Keyframes::kFistMelee1, 0.0f, false, &matrix), "a fist swing starts");
    Check(Near(matrix.m[3][0], 0.0f) && Near(matrix.m[3][1], 0.0f), "from the rest pose");
    Check(SampleKeyframes(Keyframes::kFistMelee1, 20.0f, false, &matrix), "it is under way");
    Check(matrix.m[3][1] > 1.0f, "with the hand raised");
    Check(!SampleKeyframes(Keyframes::kFistMelee1, 500.0f, false, &matrix), "and ends");

    Mtxf right;
    Mtxf left;
    SampleKeyframes(Keyframes::kFistMelee1, 25.0f, false, &right);
    SampleKeyframes(Keyframes::kFistMelee1, 25.0f, true, &left);
    Check(Near(right.m[3][0], -left.m[3][0]) && Near(right.m[3][1], left.m[3][1]), "the left hand mirrors it");
}

void TestCasingsFallAndLand() {
    Casings casings;
    CasingEject eject;
    eject.model = "models/gun/GcartridgeZ";
    eject.pistol = true;
    eject.floorY = -50.0f;
    eject.gunMatrix = TranslationMtxf({ 10.0f, 20.0f, 30.0f });
    eject.previousGunMatrix = eject.gunMatrix;
    uint32_t draws = 0;
    casings.Eject(eject, [&draws] {
        draws++;
        return 0x80000000u;
    });
    Check(draws == 7, "a pistol casing draws seven random numbers");
    int used = 0;
    for (const Casing& casing : casings.Slots()) {
        used += casing.InUse();
    }
    Check(used == 1, "one casing is in the air");

    eject.pistol = false;
    draws = 0;
    casings.Eject(eject, [&draws] {
        draws++;
        return 0x80000000u;
    });
    Check(draws == 7, "and a rifle casing draws as many");

    for (int i = 0; i < 40; i++) {
        casings.Update(1, 1.0f);
    }
    used = 0;
    for (const Casing& casing : casings.Slots()) {
        used += casing.InUse();
    }
    Check(used == 0, "casings that reach the floor are gone");

    for (int i = 0; i < 25; i++) {
        casings.Eject(eject, [] { return 0u; });
    }
    used = 0;
    for (const Casing& casing : casings.Slots()) {
        used += casing.InUse();
    }
    Check(used == static_cast<int>(Casings::kMax), "there are twenty slots");
}

} // namespace

void TestMineFusesAreShorterWithMorePlayers() {
    for (const int32_t item : { kItemRemoteMine, kItemProximityMine, kItemTimedMine, kItemBombCase }) {
        Check(ThrownItemTimer(item, 1) == 300, "a mine burns 300 ticks alone");
        for (int players = 2; players <= 4; players++) {
            Check(ThrownItemTimer(item, players) == 180, "and 180 with two or more players");
        }
    }
    Check(ThrownItemTimer(kItemPlastique, 1) == 1 && ThrownItemTimer(kItemPlastique, 3) == 1,
          "a charge's timer does not depend on the players");
    Check(ThrownItemTimer(kItemGrenade, 1) == 240 && ThrownItemTimer(kItemGrenade, 2) == 240,
          "nor does anything else's");
}

int main() {
    TestMineFusesAreShorterWithMorePlayers();
    TestPistolFiresOnceAndRecoils();
    TestMenuQuietsTheFlash();
    TestAutomaticFiresEveryThirdTick();
    TestEmptyMagazineReloadsFromAmmoHeld();
    TestReloadStatesTiltTheGun();
    TestClickyGunDryFires();
    TestDualWieldAlternatesOnFreshPresses();
    TestKnifeDrawsForItsSlash();
    TestNoiseRisesWithShotsAndDecays();
    TestKeyframesPlayToTheirEnd();
    TestCasingsFallAndLand();
    return gFailures == 0 ? 0 : 1;
}
