#include "level/Hud.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "level/Inventory.h"
#include "ship/utils/StrHash64.h"

using namespace Frigate;
using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// Item 1 is a gun that shoots 9mm; item 2 a plain melee weapon with no ammo.
const char* kTable = "item\t0\tITEM_UNARMED\t0\t0\t0\t0x110\t-\t-\t1.0\t0.0\t0.0\t0.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1.0\t0."
                     "0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t0\t0\n"
                     "item\t1\tITEM_GUN\t1\t1\t7\t0x800\t-\t-\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1."
                     "0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t0\t0\n"
                     "item\t2\tITEM_KNIFE\t1\t0\t0\t0x800\t-\t-\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t0.8\t0.9\t1."
                     "0\t0.0\t-1\t0\t0\t0\t0\t-1\t0.0\t0.0\t-\t0.0\t0.0\t0.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t0\t0\n"
                     "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\n"
                     "ammo\t1\tAMMO_9MM\t800\t0xc84\t-2.0\n";

const char* kImages = "0xc84\t5\t12\n0xd2c\t32\t32\n";

uint32_t Zero() {
    return 0;
}

// A font whose glyphs are all 8 wide and 12 tall.
std::shared_ptr<Font> MakeFont() {
    auto font = std::make_shared<Font>();
    font->Kerning.fill(0);
    for (size_t i = 0; i < Font::kNumChars; i++) {
        FontChar glyph;
        glyph.index = static_cast<int32_t>(i);
        glyph.baseline = 2;
        glyph.height = 12;
        glyph.width = 8;
        glyph.Pixels.assign(8 * 12, 0xff);
        font->Chars.push_back(glyph);
    }
    return font;
}

struct Scene {
    Inventory inventory;
    PlayerWeapons weapons;
    Hud hud;
    HudFrame frame;

    Scene() {
        weapons.Init(WeaponTable::Parse(kTable), &inventory, Zero);
        hud.SetFonts(MakeFont(), MakeFont());
        hud.SetImages(kImages);
        frame.vi = { 320, 240, 0, 30, 320, 180 };
        frame.weapons = &weapons;
    }

    // Draws a frame and returns how many commands it made.
    size_t Draw() {
        std::vector<Gfx> buffer(4096);
        Gfx* end = hud.Render(buffer.data(), frame);
        return static_cast<size_t>(end - buffer.data());
    }

    // Whether a frame calls the image list at `path`.
    bool DrawCallsImage(const char* path) {
        std::vector<Gfx> buffer(4096);
        Gfx* end = hud.Render(buffer.data(), frame);
        const uint64_t hash = CRC64(path);
        for (Gfx* command = buffer.data(); command + 1 < end; command++) {
            if ((command->words.w0 >> 24) == 0x31 && command[1].words.w0 == static_cast<uint32_t>(hash >> 32) &&
                command[1].words.w1 == static_cast<uint32_t>(hash)) {
                return true;
            }
        }
        return false;
    }
};

void TestDamageSegments() {
    GaugeVertices armour;
    MakeDamageSegments(&armour, 1, 0.35f);
    // The first segment sits at 142 degrees: sin and cos times 4 * 130 * 6 / 5.
    Check(armour[0].ob[0] == 385 && armour[0].ob[1] == 0 && armour[0].ob[2] == 491,
          "armour's outer edge at 142 degrees");
    Check(armour[1].ob[0] == 321 && armour[1].ob[2] == 409, "and its inner edge, five sixths in");
    Check(armour[0].cn[0] == 171 && armour[0].cn[1] == 227 && armour[0].cn[2] == 255, "armour shades blue");

    GaugeVertices health;
    MakeDamageSegments(&health, -1, 0.7f);
    Check(health[0].ob[0] == -383 && health[0].ob[2] == 491, "health curves the other way");
    Check(health[0].cn[0] == 255 && health[0].cn[1] == 227 && health[0].cn[2] == 57, "and shades orange");

    // 0.35 of the armour is 2.8 of 8 steps: four segments full, one part
    // full (the fraction scaled to 207 above the dimmest 48), the rest dim.
    Check(armour[6].cn[3] == 255 && armour[7].cn[3] == 255, "the first segments are lit");
    Check(armour[8].cn[3] == 213 && armour[9].cn[3] == 213, "the last lit one is part full");
    Check(armour[10].cn[3] == 48, "the rest are dim");
    // 0.7 of the health is 5.6 steps: all ten single segments and two pairs.
    Check(health[19].cn[3] == 255 && health[23].cn[3] == 255 && health[24].cn[3] == 48, "health lights 12 segments");
}

void TestMessagesQueueAndExpire() {
    Scene scene;
    scene.hud.BottomShow("first");
    scene.hud.BottomShow("second");
    Check(scene.hud.BottomMessageCount() == 2 && scene.hud.BottomMessage() == "first", "a message waits its turn");
    for (int i = 0; i < 31; i++) {
        scene.Draw();
    }
    Check(scene.hud.BottomMessage() == "first" && scene.hud.BottomMessageCount() == 2,
          "the first shows for 30 ticks with another queued");
    scene.Draw();
    Check(scene.hud.BottomMessage() == "second" && scene.hud.BottomMessageCount() == 1, "then the next takes over");
    for (int i = 0; i < 121; i++) {
        scene.Draw();
    }
    Check(scene.hud.BottomMessageCount() == 0, "a message shows for 120 ticks when it is alone");

    for (int i = 0; i < 7; i++) {
        scene.hud.BottomShow("more");
    }
    Check(scene.hud.BottomMessageCount() == 5, "the strip holds five messages");
    scene.hud.BottomShow(std::string(200, 'x'));
    scene.hud.Reset();
    scene.hud.BottomShow(std::string(200, 'x'));
    Check(scene.hud.BottomMessage().size() == 100, "a message is cut to 100 characters");

    scene.hud.TopShow("one");
    scene.hud.TopShow("two");
    scene.hud.TopShow("three");
    Check(scene.hud.TopMessageCount() == 2 && scene.hud.TopMessage() == "one", "the top strip holds two");
    for (int i = 0; i < 62; i++) {
        scene.Draw();
    }
    Check(scene.hud.TopMessage() == "two", "and moves on after 60 ticks");
}

void TestHiddenMessagesDoNotAge() {
    Scene scene;
    scene.hud.BottomShow("hidden");
    scene.hud.MessagesOff(HudFlag::NoTimer);
    for (int i = 0; i < 200; i++) {
        scene.Draw();
    }
    Check(scene.hud.BottomMessageCount() == 1, "a message does not age while the messages are off");
    scene.hud.MessagesOn(HudFlag::NoTimer);
    scene.Draw();
    Check(scene.hud.BottomMessageCount() == 1, "and starts again when they are back");
}

void TestCrosshairFollowsItsReasons() {
    Scene scene;
    Check(scene.DrawCallsImage("hud/img/0d2c"), "the crosshair shows when nothing hides it");
    scene.hud.SetSightVisible(SightReason::NotAiming, false);
    Check(!scene.DrawCallsImage("hud/img/0d2c"), "and not while the player is not aiming");
    scene.hud.SetSightVisible(SightReason::Damage, false);
    scene.hud.SetSightVisible(SightReason::NotAiming, true);
    Check(!scene.DrawCallsImage("hud/img/0d2c"), "or while another reason hides it");
    scene.hud.SetSightVisible(SightReason::Damage, true);
    Check(scene.hud.SightMode() == 0 && scene.DrawCallsImage("hud/img/0d2c"), "each reason clears alone");
}

void TestAmmoCounter() {
    Scene scene;
    // The right hand holds nothing: no counter.
    Check(!scene.DrawCallsImage("hud/img/0c84"), "an empty hand has no ammo icon");

    scene.weapons.MutableHand(HandRight).weaponNum = 1;
    scene.weapons.MutableHand(HandRight).ammoInMagazine = 7;
    scene.weapons.GiveAmmo(1, 93);
    Check(scene.DrawCallsImage("hud/img/0c84"), "a gun with ammo shows its icon");
    const size_t withCounter = scene.Draw();

    scene.hud.SetAmmoVisible(AmmoReason::Option, false);
    Check(!scene.DrawCallsImage("hud/img/0c84") && scene.Draw() < withCounter, "and it hides with the option off");
    scene.hud.SetAmmoVisible(AmmoReason::Option, true);

    scene.weapons.MutableHand(HandRight).actionState = GunState::SwitchSwap;
    Check(!scene.DrawCallsImage("hud/img/0c84"), "and while the gun is being swapped");
    scene.weapons.MutableHand(HandRight).actionState = GunState::Idle;

    scene.weapons.MutableHand(HandRight).weaponNum = 2;
    Check(!scene.DrawCallsImage("hud/img/0c84"), "a weapon with no ammo has no counter");
}

void TestGaugeBarsShowForAWhile() {
    Scene scene;
    const size_t without = scene.Draw();
    scene.hud.Health().showTime = 30;
    const size_t with = scene.Draw();
    Check(with > without, "the gauge bars draw while the health shows");
    scene.frame.watchState = 5;
    Check(scene.Draw() == without, "but not with the watch up");
}

} // namespace

int main() {
    TestDamageSegments();
    TestMessagesQueueAndExpire();
    TestHiddenMessagesDoNotAge();
    TestCrosshairFollowsItsReasons();
    TestAmmoCounter();
    TestGaugeBarsShowForAWhile();
    return gFailures == 0 ? 0 : 1;
}
