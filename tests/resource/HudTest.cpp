#include "level/Hud.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "gfx/FixedMtx.h"
#include "gfx/GbiF3dex.h"
#include "level/Inventory.h"
#include "level/Radar.h"
#include "../HudDisplayList.h"
#include "ship/utils/StrHash64.h"

using namespace Frigate;
using namespace Frigate::Level;
using Frigate::TestSupport::DrawnRect;
using Frigate::TestSupport::ReadRects;

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

// One element of a fixed-point matrix: the int and fraction halves.
float FixedElement(const Frigate::Mtx::FixedMtx& m, int row, int col) {
    const int index = (row * 2) + (col / 2);
    const bool high = col % 2 == 0;
    const auto whole = static_cast<int16_t>(static_cast<uint32_t>(m.words[index]) >> (high ? 16 : 0));
    const auto fraction = static_cast<uint16_t>(static_cast<uint32_t>(m.words[8 + index]) >> (high ? 16 : 0));
    return static_cast<float>(whole) + (static_cast<float>(fraction) / 65536.0f);
}

// Where the gauge's outer edge at 142 degrees lands on screen, in the
// projection's -1..1 range, through the two matrices the gauge pass loads.
bool GaugeEdgeOnScreen(float visibility) {
    Scene scene;
    scene.hud.Health().showTime = 30;
    scene.frame.visibilityScale = visibility;
    std::vector<Gfx> buffer(4096);
    Gfx* end = scene.hud.Render(buffer.data(), scene.frame);
    const Frigate::Mtx::FixedMtx* matrices[2] = {};
    int found = 0;
    for (Gfx* command = buffer.data(); command < end && found < 2; command++) {
        if (static_cast<int8_t>(command->words.w0 >> 24) == Fast::F3DEX_G_MTX) {
            matrices[found++] = reinterpret_cast<const Frigate::Mtx::FixedMtx*>(command->words.w1);
        }
    }
    if (found < 2) {
        return false;
    }
    const float point[4] = { 385.0f, 0.0f, 491.0f, 1.0f };
    float view[4] = {};
    float clip[4] = {};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            view[col] += point[row] * FixedElement(*matrices[1], row, col);
        }
    }
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            clip[col] += view[row] * FixedElement(*matrices[0], row, col);
        }
    }
    return std::fabs(clip[0] / clip[3]) < 1.0f && std::fabs(clip[1] / clip[3]) < 1.0f;
}

void TestGaugeSizeIgnoresVisibilityScale() {
    Check(GaugeEdgeOnScreen(1.0f), "the gauge's edge is on screen at full visibility");
    Check(GaugeEdgeOnScreen(0.2f), "and on a stage with a 0.2 visibility scale");
}

// The rectangles one frame of the HUD draws.
std::vector<DrawnRect> Rects(Scene& scene) {
    std::vector<Gfx> buffer(4096);
    Gfx* end = scene.hud.Render(buffer.data(), scene.frame);
    return ReadRects(buffer.data(), end);
}

// The black box behind the bottom message: a fill of a fully transparent black.
const DrawnRect* MessageBox(const std::vector<DrawnRect>& rects) {
    for (const DrawnRect& rect : rects) {
        if (!rect.textured && rect.colour == 0) {
            return &rect;
        }
    }
    return nullptr;
}

void TestMultiplayerMessageIsOneAndRestarts() {
    Scene scene;
    scene.hud.SetMultiplayer(true);
    scene.hud.BottomShow("first");
    scene.hud.BottomShow("second");
    Check(scene.hud.BottomMessage() == "second" && scene.hud.BottomMessageCount() == 0,
          "a multiplayer message replaces the one before and keeps no queue");
    for (int i = 0; i < 119; i++) {
        scene.Draw();
    }
    scene.hud.BottomShow("third");
    for (int i = 0; i < 119; i++) {
        scene.Draw();
    }
    Check(MessageBox(Rects(scene)) != nullptr, "a new message restarts its 120 ticks");
    scene.Draw();
    scene.Draw();
    Check(MessageBox(Rects(scene)) == nullptr, "and it is gone after them");
    scene.hud.BottomShow(std::string(200, 'x'));
    Check(scene.hud.BottomMessage().size() == 100, "a multiplayer message is cut to 100 characters too");
}

// Where the bottom message's box lands for each player of each game size.
void TestBottomMessagePlacement() {
    struct Case {
        int players;
        int player;
        int left;
        int bottom;
    };
    // The view is 30 to 210 here. Two players: 12 above the view's bottom, and
    // 8 higher again for the second. Three or four: 16 below the view's top,
    // starting at 30 in the left column and 10 in the right.
    const Case cases[] = {
        { 1, 0, 30, 198 }, { 2, 0, 30, 198 }, { 2, 1, 30, 190 }, { 3, 0, 30, 46 },
        { 3, 1, 10, 46 },  { 3, 2, 30, 46 },  { 4, 3, 10, 46 },
    };
    for (const Case& c : cases) {
        Scene scene;
        scene.hud.SetMultiplayer(c.players >= 2);
        scene.frame.playerCount = c.players;
        scene.frame.playerNumber = c.player;
        scene.hud.BottomShow("hi");
        scene.Draw();
        const std::vector<DrawnRect> rects = Rects(scene);
        const DrawnRect* box = MessageBox(rects);
        char what[96];
        std::snprintf(what, sizeof(what), "the message box of player %d of %d is at %d,%d", c.player + 1, c.players,
                      c.left, c.bottom);
        Check(box != nullptr && box->left == c.left && box->bottom == c.bottom, what);
    }

    // A left-hand weapon with ammo (or a mission clock) lifts it in the
    // two-player layouts, but not the quadrants'.
    for (const int players : { 2, 3 }) {
        Scene scene;
        scene.hud.SetMultiplayer(true);
        scene.frame.playerCount = players;
        scene.weapons.MutableHand(HandLeft).weaponNum = 1;
        scene.hud.BottomShow("hi");
        scene.Draw();
        const std::vector<DrawnRect> rects = Rects(scene);
        const DrawnRect* box = MessageBox(rects);
        Check(box != nullptr && box->bottom == (players == 2 ? 170 : 46),
              "an armed left hand lifts the message in a game of two and not of three");
    }
}

void TestAmmoCounterColumns() {
    const AmmoColumns solo = AmmoCounterColumns(1, 0);
    Check(solo.left == 59 && solo.right == 59, "one player's counters are 59 in");
    for (int player = 0; player < 2; player++) {
        const AmmoColumns two = AmmoCounterColumns(2, player);
        Check(two.left == 59 && two.right == 59, "so are two players'");
    }
    for (const int players : { 3, 4 }) {
        const AmmoColumns left = AmmoCounterColumns(players, 0);
        const AmmoColumns right = AmmoCounterColumns(players, 1);
        Check(left.left == 59 && left.right == 109, "the left column of a quad has 59 and 109");
        Check(right.left == 43 && right.right == 127, "the right column has 43 and 127");
        Check(AmmoCounterColumns(players, 2).right == 109 && AmmoCounterColumns(players, 3).right == 127,
              "the lower views follow their column");
    }

    // The right hand's icon is centred that far in from the view's right edge.
    for (const int player : { 0, 1 }) {
        Scene scene;
        scene.frame.playerCount = 3;
        scene.frame.playerNumber = player;
        scene.frame.vi = { 320, 240, player == 0 ? 0 : 161, 10, 159, 109 };
        scene.weapons.MutableHand(HandRight).weaponNum = 1;
        scene.weapons.GiveAmmo(1, 93);
        bool found = false;
        for (const DrawnRect& rect : Rects(scene)) {
            // The icon's backdrop: transparent black, the image's size plus a
            // pixel round.
            if (!rect.textured && rect.colour == 0 && rect.right - rect.left == 7 && rect.bottom - rect.top == 14) {
                const int centre = (rect.left + rect.right) / 2;
                const int want =
                    scene.frame.vi.viewLeft + scene.frame.vi.viewWidth - AmmoCounterColumns(3, player).right;
                found = std::abs(centre - want) <= 1;
            }
        }
        Check(found, "the right hand's icon sits where the column puts it");
    }
}

void TestMenuHidesTheHud() {
    Scene scene;
    scene.hud.SetMultiplayer(true);
    scene.frame.playerCount = 2;
    scene.weapons.MutableHand(HandRight).weaponNum = 1;
    scene.weapons.GiveAmmo(1, 93);
    scene.hud.BottomShow("hi");
    scene.hud.TopShow("top");
    scene.Draw();
    Check(scene.DrawCallsImage("hud/img/0d2c") && scene.DrawCallsImage("hud/img/0c84") &&
              MessageBox(Rects(scene)) != nullptr,
          "the sight, ammo counter and message show with the menu closed");
    const size_t open = scene.Draw();

    scene.frame.mpMenuOn = true;
    Check(!scene.DrawCallsImage("hud/img/0d2c") && !scene.DrawCallsImage("hud/img/0c84"),
          "an open menu hides the sight and the ammo counter");
    bool shaded = false;
    bool box = false;
    for (const DrawnRect& rect : Rects(scene)) {
        shaded = shaded || (rect.colour == 0x000000A0 && rect.left == 0 && rect.top == 30 && rect.right == 320 &&
                            rect.bottom == 210);
        box = box || (!rect.textured && rect.colour == 0);
    }
    Check(shaded, "and shades the view");
    Check(!box, "and the message");
    Check(scene.Draw() != open, "the menu changes the frame");
    for (int i = 0; i < 400; i++) {
        scene.Draw();
    }
    Check(scene.hud.BottomMessage() == "hi", "the message does not age under an open menu");
    scene.frame.mpMenuOn = false;
    Check(MessageBox(Rects(scene)) != nullptr, "and is still there after it");
}

void TestGaugesFollowTheMenuAndTheGameOver() {
    Scene scene;
    const size_t without = scene.Draw();
    scene.frame.mpMenuOn = true;
    const size_t menu = scene.Draw();
    scene.frame.gameOver = true;
    const size_t over = scene.Draw();
    Check(menu > without + 100, "the gauge bars draw while the menu is open");
    Check(over < menu, "but not once the game is over");
    scene.frame.mpMenuOn = false;
    scene.frame.gameOver = false;
    scene.hud.Health().displayTime = 5;
    scene.frame.clockTimer = 2;
    Check(scene.Draw() > without + 100 && scene.hud.Health().displayTime == 3,
          "a display time shows them and counts down");
    scene.Draw();
    scene.Draw();
    Check(scene.hud.Health().displayTime == 0, "to nothing");
}

void TestTopStripInACameraView() {
    for (const bool camera : { false, true }) {
        Scene scene;
        scene.frame.cameraBuffer = camera;
        scene.hud.TopShow("top");
        std::vector<Gfx> buffer(4096);
        Gfx* end = scene.hud.Render(buffer.data(), scene.frame);
        int top = -1;
        for (const DrawnRect& rect : ReadRects(buffer.data(), end)) {
            if (!rect.textured && (rect.colour & 0xFF) == 0x64 && rect.left == 0) {
                top = rect.top;
            }
        }
        // The strip's rectangle starts two above its text: 30 + 13, or 30 + 32
        // rounded down to a multiple of 11 and less two.
        Check(top == (camera ? 51 : 41),
              camera ? "the camera view's strip snaps to 11 pixel rows" : "the strip sits 13 below the view");
    }
}

RadarFrame TwoPlayerRadar() {
    RadarFrame radar;
    radar.playerCount = 2;
    radar.player = 0;
    radar.players[1].x = 0.0f;
    radar.players[1].z = 2000.0f;
    return radar;
}

void TestRadarLayout() {
    const ViewMetrics view{ 320, 240, 0, 10, 320, 109 };
    RadarFrame radar = TwoPlayerRadar();
    RadarLayout layout = LayoutRadar(view, radar);
    Check(layout.shown && layout.left == 279 && layout.top == 36, "the circle is 41 in from the right and 26 down");
    Check(layout.blipCount == 1 && layout.blips[0].x == 279 && layout.blips[0].y == 28,
          "a player 2000 units ahead is eight pixels up at the start");
    Check(layout.blips[0].colour == 0xFFFF00A0 && layout.selfColour == 0xFFFFFFA0, "the dots are yellow and white");

    radar.players[1].z = 8000.0f;
    layout = LayoutRadar(view, radar);
    Check(layout.blips[0].y == 20 && layout.blips[0].colour == 0xFFFF0060, "a far one sits on the rim, fainter");

    // vv_theta turns the picture: facing 90 degrees puts the one ahead to the side.
    radar.players[1].z = 2000.0f;
    radar.theta = 90.0f;
    layout = LayoutRadar(view, radar);
    Check(layout.blips[0].x == 271 && layout.blips[0].y == 36, "the picture turns with the player");
    radar.theta = -90.0f;
    layout = LayoutRadar(view, radar);
    Check(layout.blips[0].x == 287 && layout.blips[0].y == 36, "either way");
    radar.theta = 0.0f;

    radar.players[1].x = 2000.0f;
    radar.players[1].z = 0.0f;
    layout = LayoutRadar(view, radar);
    Check(layout.blips[0].x == 271 && layout.blips[0].y == 36, "one to the +x side is on the left");

    radar.players[1].dead = true;
    Check(LayoutRadar(view, radar).blipCount == 0, "a dead player is not on the radar");
    radar.players[1].dead = false;
    radar.players[0].dead = true;
    Check(!LayoutRadar(view, radar).shown, "a dead player has no radar");
    radar.players[0].dead = false;
    radar.menuOn = true;
    Check(!LayoutRadar(view, radar).shown, "nor one with the menu open");
    radar.menuOn = false;
    radar.disabled = true;
    Check(!LayoutRadar(view, radar).shown, "nor with the cheat");
    radar.disabled = false;
    radar.playerCount = 1;
    Check(!LayoutRadar(view, radar).shown, "nor a solo game");
}

void TestRadarForThreeAndFour() {
    const ViewMetrics lowerRight{ 320, 240, 161, 121, 159, 109 };
    const ViewMetrics lowerLeft{ 320, 240, 0, 121, 159, 109 };
    RadarFrame radar;
    radar.playerCount = 4;
    for (int player = 0; player < 4; player++) {
        radar.players[static_cast<size_t>(player)].x = 1000.0f * static_cast<float>(player);
    }
    radar.player = 3;
    RadarLayout layout = LayoutRadar(lowerRight, radar);
    Check(layout.left == 279 && layout.top == 147 && layout.blipCount == 3, "the right column's radar is 41 in");
    radar.player = 2;
    layout = LayoutRadar(lowerLeft, radar);
    Check(layout.left == 133 && layout.top == 147 && layout.blipCount == 3,
          "and the left column's is pushed 15 pixels right of that");
    Check(layout.blips[0].colour == 0xFFFF00A0 && layout.blips[2].colour == 0xFFFF00A0,
          "with a dot for each of the others");
}

void TestRadarColoursByTeamAndItem() {
    const ViewMetrics view{ 320, 240, 0, 10, 320, 109 };
    RadarFrame radar;
    radar.playerCount = 3;
    radar.teamColours = true;
    radar.players[1].x = 1000.0f;
    radar.players[2].x = 9000.0f;
    RadarLayout layout = LayoutRadar(view, radar);
    Check(layout.selfColour == 0xFF7777FF, "a player without the item or on the first team is red");
    Check(layout.blips[0].colour == 0xFF0000A0 && layout.blips[1].colour == 0xFF000060,
          "and so are the others, fainter at the rim");
    radar.players[0].haveTokenOrGoldenGun = 1;
    radar.players[1].haveTokenOrGoldenGun = 1;
    radar.players[2].haveTokenOrGoldenGun = 1;
    layout = LayoutRadar(view, radar);
    Check(layout.selfColour == 0x8888FFFF, "a holder or the second team is blue");
    Check(layout.blips[0].colour == 0x2828FFFF && layout.blips[1].colour == 0x2828FFB0, "and so are the others");
}

void TestRadarDraws() {
    const ViewMetrics view{ 320, 240, 0, 10, 320, 109 };
    RadarFrame radar = TwoPlayerRadar();
    std::vector<Gfx> buffer(512);
    Gfx* end = RenderRadar(buffer.data(), view, radar);
    const std::vector<DrawnRect> rects = ReadRects(buffer.data(), end);
    // The circle, the player's shadow and dot, and the other player's.
    Check(rects.size() == 5 && rects[0].textured && rects[0].left == 263 && rects[0].top == 20 &&
              rects[0].right == 295 && rects[0].bottom == 52,
          "the radar draws its 32 by 32 circle and two dots each");
    Check(rects.size() == 5 && rects[1].colour == 0x40 && rects[2].colour == 0xFFFFFFA0 &&
              rects[4].colour == 0xFFFF00A0,
          "in shadow and colour");
    radar.menuOn = true;
    Check(RenderRadar(buffer.data(), view, radar) == buffer.data(), "an open menu draws nothing");
}

} // namespace

int main() {
    TestDamageSegments();
    TestMessagesQueueAndExpire();
    TestHiddenMessagesDoNotAge();
    TestCrosshairFollowsItsReasons();
    TestAmmoCounter();
    TestGaugeBarsShowForAWhile();
    TestGaugeSizeIgnoresVisibilityScale();
    TestMultiplayerMessageIsOneAndRestarts();
    TestBottomMessagePlacement();
    TestAmmoCounterColumns();
    TestMenuHidesTheHud();
    TestGaugesFollowTheMenuAndTheGameOver();
    TestTopStripInACameraView();
    TestRadarLayout();
    TestRadarForThreeAndFour();
    TestRadarColoursByTeamAndItem();
    TestRadarDraws();
    return gFailures == 0 ? 0 : 1;
}
