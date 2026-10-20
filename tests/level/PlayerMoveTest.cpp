#include <cmath>
#include <cstdio>
#include <string>

#include "StanFixture.h"
#include "level/Objects.h"
#include "level/PlayerMove.h"
#include "level/PlayerWeapons.h"
#include "level/Stan.h"
#include "level/WeaponTable.h"

using namespace Frigate::Level;
using namespace StanFixture;

namespace Frigate::Level {
// Puts the player against a tank without a prop collision to bump it.
struct PlayerMoveTestAccess {
    static void SetWorldTank(PlayerMove& player, int prop) {
        player.worldTank_ = prop;
    }
    static float TankTurretTurn(const PlayerMove& player) {
        return player.tankTurretTurn_;
    }
};
} // namespace Frigate::Level

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) < tolerance;
}

// A head that moves the player by a fixed amount each tick, standing in for
// the gait animation.
class FixedHead final : public HeadMotion {
  public:
    float x = 0.0f;
    float z = 0.0f;
    Vec3 Tick(float, float, float, float, const MoveContext&) override {
        return { x, 168.0f, z };
    }
};

// Bond on the first tile of a floor covering 0..1000, one unit above it.
struct Scene {
    Stan stan;
    PlayerMove player;
    MoveContext context;

    explicit Scene(int16_t rise = 0, Vec3 position = { 500.0f, 1.0f, 800.0f }, float theta = 0.0f,
                   float remainder = 0) {
        stan.Load(MakeFloor(1000, 0, rise, 2), 1.0f);
        context.random = [] { return uint32_t{ 0 }; };
        PlayerSpawn spawn;
        spawn.position = position;
        spawn.theta = theta;
        const float radians = theta * 0.017453292f;
        spawn.look = { -std::sin(radians), 0.0f, std::cos(radians) };
        spawn.stanTile = position[0] <= position[2] ? 0 : 1;
        spawn.stanHeight = 0.0f;
        spawn.stanHeightRemainder = remainder;
        spawn.eyeHeight = 175.0f;
        player.Init(spawn, stan);
    }

    void Tick(const MoveInput& input = {}, int times = 1) {
        for (int i = 0; i < times; i++) {
            player.Tick(context, input, stan);
        }
    }
};

void TestStandingStill() {
    Scene scene;
    scene.Tick({}, 5);
    Check(Near(scene.player.CollisionPosition()[0], 500.0f) && Near(scene.player.CollisionPosition()[2], 800.0f),
          "a player given no input stays where it is");
    Check(scene.player.Tile() == 0 && scene.player.Room() == 2, "on its tile and in its room");
    Check(Near(scene.player.EyeHeight(), 175.0f) && Near(scene.player.CollisionPosition()[1], 175.0f),
          "the standing eye height is above the floor");
    Check(Near(scene.player.Position()[1], 175.0f - 4.0f * 0.0f, 20.0f), "the camera is at the eye");
}

void TestTurning() {
    Scene scene;
    MoveInput input;
    input.stickX = 60;
    scene.Tick(input);
    // A stick pushed 55 past the dead zone turns at (55 / 70)^2 of the top speed.
    Check(Near(scene.player.SpeedTheta(), (55.0f / 70.0f) * (55.0f / 70.0f)),
          "the turn speed follows the stick squared");
    Check(Near(scene.player.Theta(), scene.player.SpeedTheta() * 3.5f), "the view turns by the speed");
    Check(scene.player.LookDirection()[0] < 0.0f && Near(scene.player.LookDirection()[1], 0.0f),
          "turning right swings the look direction to -x");

    input.stickX = -100 + 1;
    scene.Tick(input, 400);
    Check(scene.player.Theta() >= 0.0f && scene.player.Theta() < 360.0f, "the heading wraps into 0..360");
}

void TestSpawnState() {
    Scene scene(0, { 500.0f, 1.0f, 800.0f }, 30.0f);
    const Vec3 look = scene.player.AppliedView();
    const Vec3 up = scene.player.AppliedViewUp();
    Check(Near(look[0], 1.0f) && Near(look[1], 0.0f) && Near(look[2], 0.0f),
          "change_player_pos_to_target leaves the applied view along +x");
    Check(Near(up[0], 0.0f) && Near(up[1], 1.0f) && Near(up[2], 0.0f), "and the up vector along +y");
    const float theta = 30.0f * 0.017453292f;
    Check(Near(scene.player.LookDirection()[0], -std::sin(theta)) &&
              Near(scene.player.LookDirection()[2], std::cos(theta)),
          "the look direction is the spawn heading");
}

void TestAppliedView() {
    Scene scene(0, { 500.0f, 1.0f, 800.0f }, 30.0f);
    MoveInput input;
    input.stickY = 80;
    scene.Tick(input, 3);
    const float theta = scene.player.Theta() * 0.017453292f;
    const float verta = scene.player.Verta() * 0.017453292f;
    const Vec3 look = scene.player.AppliedView();
    const Vec3 up = scene.player.AppliedViewUp();
    Check(Near(look[0], -std::sin(theta) * std::cos(verta)) && Near(look[1], std::sin(verta)) &&
              Near(look[2], std::cos(theta) * std::cos(verta)),
          "with the standing head the view follows the heading and pitch");
    Check(Near(look[0] * up[0] + look[1] * up[1] + look[2] * up[2], 0.0f) && up[1] > 0.0f,
          "and the up vector is upright and square to it");

    // A head tilted to one side rolls the view about its look direction.
    class TiltedHead final : public HeadMotion {
      public:
        Vec3 Tick(float, float, float, float, const MoveContext&) override {
            return { 0.0f, 168.0f, 0.0f };
        }
        Vec3 Look() const override {
            return { 0.0f, 0.0f, 1.0f };
        }
        Vec3 Up() const override {
            return { 0.6f, 0.8f, 0.0f };
        }
    } tilted;
    scene.player.SetHead(&tilted);
    scene.Tick();
    const Vec3 rolled = scene.player.AppliedViewUp();
    Check(!Near(rolled[0], up[0], 0.1f) || !Near(rolled[2], up[2], 0.1f), "a tilted head rolls the up vector");
    const Vec3& tiltedLook = scene.player.AppliedView();
    Check(Near(tiltedLook[0] * tiltedLook[0] + tiltedLook[1] * tiltedLook[1] + tiltedLook[2] * tiltedLook[2], 1.0f),
          "and the look direction stays a unit vector");
}

void TestWalkSpeed() {
    Scene scene;
    MoveInput input;
    input.stickY = 80;
    scene.Tick(input);
    Check(Near(scene.player.SpeedForwards(), 1.08f), "a full stick is a speed of 1.08");
    scene.Tick(input, 400);
    Check(scene.player.SpeedForwards() > 1.2f, "holding forward for three seconds adds the run boost");

    input.stickY = 0;
    scene.Tick(input, 400);
    Check(Near(scene.player.SpeedForwards(), 0.0f) && Near(scene.player.LastMove().analogWalk, 0.0f),
          "releasing the stick stops");
}

void TestHeadMotionMovesThePlayer() {
    Scene scene;
    FixedHead head;
    head.z = 20.0f;
    scene.player.SetHead(&head);

    scene.Tick({}, 3);
    Check(Near(scene.player.CollisionPosition()[2], 860.0f) && Near(scene.player.CollisionPosition()[0], 500.0f),
          "the head's forward motion moves the player along its look direction");
    Check(Near(scene.player.PreviousPosition()[2], 840.0f), "the previous position is where the tick started");
}

void TestWallStopsThePlayer() {
    Scene scene;
    FixedHead head;
    head.z = 20.0f;
    scene.player.SetHead(&head);

    scene.Tick({}, 40);
    // The floor's edge is at z 1000, and the player's circle has a radius of 30.
    const float z = scene.player.CollisionPosition()[2];
    Check(z > 940.0f && z <= 970.5f, "the player walks up to the wall but not into it");
    Check(scene.player.Tile() == 0, "on the same tile");
}

void TestSlidingAlongAWall() {
    // Facing 45 degrees off the wall, the push along it carries the player on.
    Scene scene(0, { 500.0f, 1.0f, 900.0f }, 45.0f);
    FixedHead head;
    head.z = 10.0f;
    scene.player.SetHead(&head);

    scene.Tick({}, 40);
    const Vec3& position = scene.player.CollisionPosition();
    Check(position[0] < 400.0f, "the player slides along the wall");
    Check(position[2] <= 970.5f, "without going through it");
}

void TestCrossingToTheNextTile() {
    // Walking +x up a slope of one in ten.
    Scene scene(100, { 300.0f, 1.0f, 700.0f }, -90.0f);
    FixedHead head;
    head.z = 20.0f;
    scene.player.SetHead(&head);

    scene.Tick({}, 10);
    Check(Near(scene.player.CollisionPosition()[0], 500.0f, 0.01f) && scene.player.Tile() == 0,
          "still on the first tile");
    Check(Near(scene.player.StanHeight(), 50.0f, 0.01f), "the floor rises with x");
    Check(scene.player.CrouchPosition() == kCrouchStand, "standing");

    scene.Tick({}, 15);
    Check(scene.player.Tile() == 1 && scene.player.CollisionPosition()[0] > 700.0f, "walked onto the next tile");
    Check(Near(scene.player.StanHeight(), scene.player.CollisionPosition()[0] / 10.0f, 0.05f),
          "the floor height follows onto it");
    Check(scene.player.Room() == 2, "in the same room");
    Check(scene.player.StanHeightRemainder() <= scene.player.StanHeight() + 0.001f, "the eye rises smoothly");
}

void TestFalling() {
    // Spawned 100 above the floor, the player drops and lands.
    Scene scene(0, { 500.0f, 1.0f, 800.0f }, 0.0f, 100.0f);
    scene.Tick({}, 5);
    const float after = scene.player.StanHeightRemainder();
    Check(after < 100.0f && after > 0.0f, "it falls");
    scene.Tick({}, 3);
    Check(scene.player.StanHeightRemainder() < after, "and keeps falling");
    scene.Tick({}, 60);
    Check(Near(scene.player.StanHeightRemainder(), 0.0f), "it lands on the floor");
    Check(Near(scene.player.CollisionPosition()[1], 175.0f, 5.0f), "and the eye settles above it");
}

void TestCrouching() {
    Scene scene;
    MoveInput input;
    // Aim mode (L) with the down pad crouches.
    input.buttons = Buttons::L | Buttons::DownC;
    scene.Tick(input, 40);
    Check(scene.player.InSightAim() && scene.player.CrouchPosition() == kCrouchSquat,
          "aiming and pressing down crouches");
    Check(Near(scene.player.DuckingHeightOffset(), -100.0f), "the view drops to the crouch height");
    Check(scene.player.CollisionPosition()[1] < 100.0f, "and the eye with it");

    input.buttons = Buttons::L;
    scene.Tick(input, 40);
    Check(scene.player.CrouchPosition() == kCrouchStand && Near(scene.player.DuckingHeightOffset(), 0.0f),
          "releasing the pad stands the player back up");

    // A weapon that disables crouching is not crouched with.
    Scene locked;
    locked.context.weaponDisablesCrouch = true;
    locked.Tick(input, 5);
    input.buttons = Buttons::L | Buttons::DownC;
    locked.Tick(input, 40);
    Check(locked.player.CrouchPosition() == kCrouchStand, "a weapon that disables crouching keeps the player standing");
}

void TestLeaning() {
    Scene scene;
    MoveInput input;
    // Aiming with the left pad leans left, which shifts the player along -sway.
    input.buttons = Buttons::L | Buttons::LeftC;
    scene.Tick(input, 30);
    Check(scene.player.LastMove().rLeanLeft, "aiming and pressing left leans");
    Check(scene.player.CollisionPosition()[0] > 500.0f, "the lean moves the player sideways");

    input.buttons = Buttons::L;
    scene.Tick(input, 60);
    Check(Near(scene.player.CollisionPosition()[0], 500.0f, 1.0f), "and back again once released");
}

void TestControlStyles() {
    // 1.2: the pad steps forward and back and turns.
    Scene solitare;
    solitare.context.options.controlType = ControlConfig::Solitare;
    MoveInput input;
    input.buttons = Buttons::UpC;
    solitare.Tick(input, 3);
    Check(solitare.player.LastMove().digitalStepForward && solitare.player.SpeedForwards() > 0.0f,
          "control style 1.2 steps forward with the pad");
    Check(solitare.player.LastMove().disableLookAhead, "and does not look ahead");

    // 2.1: the second controller aims and the stick strafes.
    Scene plenty;
    plenty.context.options.controlType = ControlConfig::Plenty;
    MoveInput two;
    two.stick2X = 40;
    two.buttons2 = Buttons::Z;
    plenty.Tick(two);
    Check(plenty.player.InSightAim() && plenty.player.LastMove().canManualAim,
          "control style 2.1 aims with the second controller's trigger");
    Check(plenty.player.SpeedSideways() > 0.0f, "and strafes with its stick");

    // Aim toggles rather than holds when the option says so.
    Scene toggle;
    toggle.context.options.aimToggle = true;
    MoveInput press;
    press.buttons = Buttons::L;
    toggle.Tick(press);
    toggle.Tick({});
    Check(toggle.player.InSightAim(), "a toggled aim stays on after the button is released");
    toggle.Tick(press);
    Check(!toggle.player.InSightAim(), "and turns off with the next press");
}

// One tick of `style` under `input`: the move it asked for and whether it is
// aiming.
struct StyleResult {
    MoveData move;
    bool aiming = false;
};

StyleResult DriveStyle(ControlConfig style, const MoveInput& input) {
    Scene scene;
    scene.context.options.controlType = style;
    scene.Tick(input);
    return { scene.player.LastMove(), scene.player.InSightAim() };
}

// bondviewProcessInput under the four one-controller styles: 1.1 Honey and 1.3
// Kissy read the pad as a look pad, 1.2 Solitare and 1.4 Goodnight as a walk
// pad; 1.3 and 1.4 swap Z and A's roles and aim with Z.
void TestOneControllerStyles() {
    const ControlConfig styles[] = { ControlConfig::Honey, ControlConfig::Solitare, ControlConfig::Kissy,
                                     ControlConfig::Goodnight };
    for (const ControlConfig style : styles) {
        const bool zAims = style == ControlConfig::Kissy || style == ControlConfig::Goodnight;
        const bool walkPad = style == ControlConfig::Solitare || style == ControlConfig::Goodnight;

        MoveInput stick;
        stick.stickX = 40;
        stick.stickY = 80;
        stick.stick2X = 90;
        stick.stick2Y = 90;
        stick.buttons2 = Buttons::Z | Buttons::A | Buttons::B;
        const StyleResult moving = DriveStyle(style, stick);
        Check(moving.move.analogTurn == 35 && moving.move.analogStrafe == 35 && moving.move.analogWalk == 75 &&
                  moving.move.analogPitch == 75,
              "the stick, less its dead zone, drives every axis");
        Check(!moving.move.triggerOn && !moving.aiming && !moving.move.btap, "and the second controller is ignored");

        MoveInput z;
        z.buttons = Buttons::Z;
        const StyleResult zPressed = DriveStyle(style, z);
        Check(zPressed.move.triggerOn == !zAims && zPressed.aiming == zAims, "Z shoots, or aims in 1.3 and 1.4");

        MoveInput a;
        a.buttons = Buttons::A;
        const StyleResult aPressed = DriveStyle(style, a);
        Check(aPressed.move.triggerOn == zAims && !aPressed.aiming, "A shoots in 1.3 and 1.4, and only there");

        MoveInput l;
        l.buttons = Buttons::L;
        Check(DriveStyle(style, l).aiming == !zAims, "L aims in 1.1 and 1.2");
        MoveInput r;
        r.buttons = Buttons::R;
        Check(DriveStyle(style, r).aiming == !zAims, "and R");

        MoveInput shootAndInventory;
        shootAndInventory.buttons = zAims ? (Buttons::A | Buttons::L) : (Buttons::Z | Buttons::A);
        Check(!DriveStyle(style, shootAndInventory).move.triggerOn, "the inventory button holds the trigger off");

        MoveInput b;
        b.buttons = Buttons::B;
        Check(DriveStyle(style, b).move.btap, "B taps");

        MoveInput left;
        left.buttons = Buttons::LeftC;
        const MoveData leftMove = DriveStyle(style, left).move;
        Check(leftMove.digitalStepLeft, "the left pad steps left");
        Check((leftMove.tankTurnLeftSpeed == 1.0f) == !walkPad, "and turns the tank in 1.1 and 1.3");
        Check(leftMove.disableLookAhead == walkPad, "1.2 and 1.4 never look ahead");

        MoveInput up;
        up.buttons = Buttons::UpC;
        const MoveData upMove = DriveStyle(style, up).move;
        Check(upMove.digitalStepForward == walkPad, "the up pad steps forward in 1.2 and 1.4");
        Check((upMove.speedVertaDown == 1.0f) == !walkPad, "and looks up in 1.1 and 1.3");
        MoveInput dpadUp;
        dpadUp.buttons = Buttons::UpPad;
        Check(DriveStyle(style, dpadUp).move.digitalStepForward == walkPad, "the D-pad acts like the C buttons");

        MoveInput aimStick;
        aimStick.buttons = zAims ? Buttons::Z : Buttons::L;
        aimStick.stickX = -65;
        aimStick.stickY = 80;
        const StyleResult aimed = DriveStyle(style, aimStick);
        Check(aimed.aiming && aimed.move.canManualAim && !aimed.move.canSwivelGun, "aiming switches to manual aim");
        Check(Near(aimed.move.aimTurnLeftSpeed, 0.5f), "the stick past 60 pans the aim");
        Check(Near(aimed.move.speedVertaDown, 1.0f), "and tilts it, capped at one");

        MoveInput aimLean;
        aimLean.buttons = (zAims ? Buttons::Z : Buttons::L) | Buttons::RightC;
        Check(DriveStyle(style, aimLean).move.rLeanRight, "aiming and pressing right leans");

        MoveInput aimCrouch;
        aimCrouch.buttons = (zAims ? Buttons::Z : Buttons::L) | Buttons::DownC;
        Check(DriveStyle(style, aimCrouch).move.crouchDown, "aiming and pressing down crouches");
    }
}

// The four two-controller styles: 2.1 Plenty and 2.3 Domino spend the second
// stick on strafing and pitch, 2.2 Galore and 2.4 Goodhead on strafing and
// walking; 2.1 and 2.2 aim with the second Z, 2.3 and 2.4 shoot with it.
void TestTwoControllerStyles() {
    const ControlConfig styles[] = { ControlConfig::Plenty, ControlConfig::Galore, ControlConfig::Domino,
                                     ControlConfig::Goodhead };
    for (const ControlConfig style : styles) {
        const bool strafeAndPitch = style == ControlConfig::Plenty || style == ControlConfig::Domino;
        const bool secondAims = style == ControlConfig::Plenty || style == ControlConfig::Galore;

        MoveInput sticks;
        sticks.stickX = 40;
        sticks.stickY = 80;
        sticks.stick2X = 30;
        sticks.stick2Y = -60;
        const MoveData move = DriveStyle(style, sticks).move;
        Check(move.analogTurn == 35 && move.analogStrafe == 25, "the second stick strafes");
        if (strafeAndPitch) {
            Check(move.analogWalk == 75 && move.analogPitch == -55, "and pitches, the first stick walking");
        } else {
            Check(move.analogWalk == -55 && move.analogPitch == 75, "and walks, the first stick pitching");
        }
        Check(move.disableLookAhead, "two-controller styles never look ahead");

        MoveInput firstZ;
        firstZ.buttons = Buttons::Z;
        const StyleResult first = DriveStyle(style, firstZ);
        Check(first.aiming == !secondAims && first.move.triggerOn == secondAims, "the first Z");
        MoveInput secondZ;
        secondZ.buttons2 = Buttons::Z;
        const StyleResult second = DriveStyle(style, secondZ);
        Check(second.aiming == secondAims && second.move.triggerOn == !secondAims, "and the second Z do opposite jobs");

        MoveInput l;
        l.buttons = Buttons::L;
        l.buttons2 = Buttons::R;
        Check(!DriveStyle(style, l).aiming, "L and R aim nothing");

        MoveInput firstB;
        firstB.buttons = Buttons::B;
        MoveInput secondB;
        secondB.buttons2 = Buttons::B;
        Check(DriveStyle(style, firstB).move.btap && DriveStyle(style, secondB).move.btap,
              "B taps from either controller");

        MoveInput shootWithA;
        shootWithA.buttons = secondAims ? Buttons::Z : 0;
        shootWithA.buttons2 = secondAims ? 0 : Buttons::Z;
        shootWithA.buttons2 |= Buttons::A;
        Check(!DriveStyle(style, shootWithA).move.triggerOn, "A on the second controller holds the trigger off");

        MoveInput aimed;
        aimed.buttons = secondAims ? 0 : Buttons::Z;
        aimed.buttons2 = secondAims ? Buttons::Z : 0;
        aimed.stickX = -65;
        aimed.stick2Y = -40;
        const StyleResult aiming = DriveStyle(style, aimed);
        Check(aiming.aiming && aiming.move.canManualAim, "aiming switches to manual aim");
        Check(Near(aiming.move.aimTurnLeftSpeed, 0.5f), "the first stick pans it");
        Check(aiming.move.crouchDown, "and the second stick crouches");
        aimed.stick2Y = 40;
        Check(DriveStyle(style, aimed).move.crouchUp, "or stands");

        // With no second controller the second pad's controls are idle: the
        // style neither walks nor looks from the first stick alone.
        MoveInput alone;
        alone.stickX = 40;
        alone.stickY = 80;
        const MoveData one = DriveStyle(style, alone).move;
        Check(one.analogStrafe == 0, "a missing second controller can't strafe");
        Check(one.analogWalk == (strafeAndPitch ? 75 : 0) && one.analogPitch == (strafeAndPitch ? 0 : 75),
              "and its axis is the one the first stick doesn't have");
    }

    // The two-controller reads only happen under a two-controller style.
    MoveInput second;
    second.stick2X = 60;
    second.buttons2 = Buttons::Z;
    Check(!DriveStyle(ControlConfig::Honey, second).aiming &&
              DriveStyle(ControlConfig::Honey, second).move.analogStrafe == 0,
          "the second controller is only read by 2.1 to 2.4");
}

void TestLookAhead() {
    // The view pitches up to the slope ahead while walking forward.
    Scene scene(400, { 200.0f, 1.0f, 500.0f }, -90.0f);
    MoveInput input;
    input.stickY = 80;
    scene.Tick(input, 5);
    Check(scene.player.Verta() >= -90.0f && scene.player.Verta() <= 90.0f, "the pitch stays in range");

    // Pressing up on the pad pitches the view (control style 1.1).
    Scene manual;
    MoveInput pitch;
    pitch.buttons = Buttons::UpC;
    manual.Tick(pitch, 5);
    Check(manual.player.LastMove().speedVertaDown > 0.0f, "the pad pitches the view");
    Check(manual.player.Verta() < -4.0f, "and the pitch changes");
}

// A watch that is in whatever state the test says, and counts what the tick
// tells it.
class FakeWatch final : public WatchHooks {
  public:
    int32_t state = 0;
    int inputs = 0;
    int startPresses = 0;
    int zoomUpdates = 0;
    int ticks = 0;
    float zoomFovY = 60.0f;

    int32_t State() const override {
        return state;
    }
    void OnInput(bool startPressed, bool blocked) override {
        inputs++;
        startPresses += startPressed ? 1 : 0;
        // A Start press opens it, unless the player cannot use the watch.
        if (startPressed && !blocked && state == 0) {
            state = 1;
        }
    }
    void UpdateZoom(float) override {
        zoomUpdates++;
    }
    float ZoomFovY() const override {
        return zoomFovY;
    }
    void Tick() override {
        ticks++;
    }
};

void TestTheWatchGatesTheControls() {
    Scene scene;
    FakeWatch watch;
    scene.context.watch = &watch;

    MoveInput walk;
    walk.stickY = 80;
    scene.Tick(walk, 3);
    Check(scene.player.SpeedForwards() > 0.0f && watch.zoomUpdates == 3 && watch.ticks == 0,
          "a closed watch leaves the controls and keeps the zoom at the standard view");

    // Start opens it: from that frame the stick is not read and the watch ticks.
    Scene opened;
    FakeWatch opening;
    opened.context.watch = &opening;
    MoveInput start = walk;
    start.buttons = Buttons::Start;
    opened.Tick(start);
    Check(opening.startPresses == 1 && opening.state == 1, "a Start press reaches the watch");
    Check(opened.player.SpeedForwards() == 0.0f && opening.ticks == 1 && opening.zoomUpdates == 0,
          "and the frame it opens on reads no controls and steps the watch");

    // Held, Start is not a new press.
    opened.Tick(start);
    Check(opening.startPresses == 1, "a held Start is not pressed again");

    // The turn and the pitch stick are left alone with the watch up.
    Scene turning;
    FakeWatch up;
    up.state = 5;
    turning.context.watch = &up;
    MoveInput turn;
    turn.stickX = 60;
    turning.Tick(turn, 3);
    Check(turning.player.SpeedTheta() == 0.0f && Near(turning.player.Theta(), 0.0f),
          "an open watch does not turn the player");
}

void TestTurnReadsTheZoomReachedThisTick() {
    Scene scene;
    FakeWatch watch;
    watch.zoomFovY = 30.0f;
    scene.context.watch = &watch;
    float handedToCamera = 0.0f;
    scene.context.setFovY = [&](float fovY) { handedToCamera = fovY; };
    MoveInput turn;
    turn.stickX = 100;
    scene.Tick(turn);
    // The context starts the tick at the standard 60; the zoom update replaces
    // it, and the full-stick turn is scaled by the zoom, not by the 60.
    Check(Near(scene.player.SpeedTheta(), 0.5f), "the turn is scaled by the zoom this tick reached");
    Check(handedToCamera == 30.0f, "and the camera is given the same field of view");
}

void TestParseControlStyle() {
    const char* names[] = { "1.1", "1.2", "1.3", "1.4", "2.1", "2.2", "2.3", "2.4" };
    for (int style = 0; style < 8; style++) {
        const auto parsed = ParseControlStyle(names[style]);
        Check(parsed && static_cast<int>(*parsed) == style, "each style name is its ControlConfig");
        Check(parsed && PlayerOptions::FromSaveBits(static_cast<uint16_t>(style << 8)).controlType == *parsed,
              "and the save option bits agree");
    }
    Check(!ParseControlStyle("1.5") && !ParseControlStyle("3.1") && !ParseControlStyle("2.0") &&
              !ParseControlStyle("11") && !ParseControlStyle("") && !ParseControlStyle("1.10"),
          "anything else is not a style");
}

void TestOptionsFromSaveBits() {
    // 0x47A: auto aim, sight, look ahead, ammo, widescreen, and 2.1.
    const PlayerOptions options = PlayerOptions::FromSaveBits(0x047A);
    Check(options.controlType == ControlConfig::Plenty, "the control style is bits 8 to 10");
    Check(options.autoAim && options.sightOnScreen && options.lookAhead && options.ammoOnScreen,
          "the on-screen options are single bits");
    Check(!options.lookInverted && !options.aimToggle && !options.ratio16x9, "and the others are off");
    Check(options.screen == ScreenSize::Wide && options.WideScreen(), "the screen is wide");
    Check(PlayerOptions::FromSaveBits(0x0800).screen == ScreenSize::Cinema, "or cinema");
    Check(PlayerOptions::FromSaveBits(0x0080).ratio16x9 && PlayerOptions::FromSaveBits(0x0080).WideScreen(),
          "16:9 counts as wide for the guns");
    const PlayerOptions plain = PlayerOptions::FromSaveBits(0x0001);
    Check(plain.lookInverted && !plain.sightOnScreen && plain.screen == ScreenSize::Full && !plain.WideScreen(),
          "an inverted look with everything else off");
}

} // namespace

void TestPlayerProp() {
    Scene scene;
    Objects objects;
    objects.Reset(3, &scene.stan, nullptr, nullptr);
    scene.stan.SetProps(&objects);
    PropList& props = objects.Props();
    const int prop = props.Allocate();
    props[prop].type = PropType::Viewer;
    props.Activate(prop);
    props.Enable(prop);
    objects.SetViewerFootprint([&scene](int, PropCollider* out) { return scene.player.Footprint(out); });
    scene.player.SetProp(&props, prop);

    FixedHead head;
    head.z = 20.0f;
    scene.player.SetHead(&head);
    scene.Tick({}, 3);
    Check(Near(scene.player.CollisionPosition()[2], 860.0f), "the player's own footprint does not stop its walk");
    Check(props[prop].stan == scene.player.Tile() && props[prop].position == scene.player.CollisionPosition(),
          "the prop takes the player's tile and collision position");
    const int room[] = { 2, -1 };
    Check(props.RoomProps(room) == std::vector<int16_t>({ static_cast<int16_t>(prop) }),
          "and is registered in the player's room, once");

    PropCollider footprint;
    Check(scene.player.PropCollides() && scene.player.Footprint(&footprint) && footprint.edges == 4 &&
              Near(footprint.polygon[0][0], scene.player.CollisionPosition()[0] + 30.0f) &&
              footprint.top > footprint.bottom,
          "the footprint is a diamond around the player, between its feet and its head");

    // Something else that tests against the player sees it, unless it looks at objects only.
    int tile = scene.player.Tile();
    const Vec3& at = scene.player.CollisionPosition();
    Check(scene.stan.TestVolume(&tile, at[0] + 35.0f, at[2], 10.0f, CdType::Players, 0.0f, 1.0f) == 2 &&
              scene.stan.LastCollision().prop == prop,
          "another mover's circle at the edge of the footprint is a hit");
    Check(scene.stan.TestVolume(&tile, at[0] + 35.0f, at[2], 10.0f, CdType::Objs, 0.0f, 1.0f) < 0,
          "but not one that leaves players out");
}

// A tank that holds the player's footprint, so the player can get in.
class FakeTankHooks final : public TankHooks {
  public:
    PlacedObject tank;
    TankGeometry geometry;
    int entered = 0;
    int left = 0;

    FakeTankHooks() {
        tank.tank.rect = { { { 0, 0 }, { 0, 2000 }, { 2000, 2000 }, { 2000, 0 }, {}, {}, {}, {} } };
        tank.tank.rectEdges = 4;
        tank.scale = 1.0f;
    }
    PlacedObject* TankOf(int prop) override {
        return prop == 0 ? &tank : nullptr;
    }
    const TankGeometry* Geometry(const PlacedObject&) override {
        return &geometry;
    }
    const ModelBounds* Hull(const PlacedObject&) override {
        return nullptr;
    }
    bool CollisionBounds(int, PropCollider* out) override {
        out->polygon = tank.tank.rect;
        out->edges = 4;
        return true;
    }
    void SetCollides(int, bool) override {
    }
    void EnterTank(int32_t) override {
        entered++;
    }
    int32_t LeaveTank() override {
        left++;
        return 0;
    }
    int TickTank(const Frame&) override {
        return 0;
    }
};

// What the stage branches on after a tick: B and the Modern Interact reach
// UseProp, only B may then fall back to a reload, and the Modern Reload stands
// alone.
struct Outcome {
    bool use;
    bool reloadAfterFailedUse;
    bool reload;
};

Outcome Press(Scene& scene, const MoveInput& input) {
    scene.Tick(input);
    return { scene.player.UsePressed(), scene.player.UsePressed() && scene.player.UseFallsBackToReload(),
             scene.player.ReloadPressed() };
}

void TestInteractAndReloadAreSeparate() {
    MoveInput b;
    b.buttons = Buttons::B;
    {
        Scene scene;
        const Outcome out = Press(scene, b);
        Check(out.use && out.reloadAfterFailedUse && !out.reload, "B uses and falls back to reload, as in the decomp");
        Check(!Press(scene, b).use, "a held B does not tap again");
    }
    {
        Scene scene;
        MoveInput interact;
        interact.interactPressed = true;
        const Outcome out = Press(scene, interact);
        Check(out.use && !out.reloadAfterFailedUse && !out.reload,
              "Interact uses a prop (a door) but never reloads when nothing is there");
        Check(!scene.player.LastMove().btap, "and is not a B tap");
    }
    {
        Scene scene;
        MoveInput reload;
        reload.reloadPressed = true;
        const Outcome out = Press(scene, reload);
        Check(!out.use && !out.reloadAfterFailedUse && out.reload, "Reload reloads and uses nothing, so no door opens");
    }
    {
        Scene scene;
        const Outcome out = Press(scene, {});
        Check(!out.use && !out.reloadAfterFailedUse && !out.reload, "with the fields unset nothing is requested");
    }
    {
        Scene scene;
        MoveInput both = b;
        both.interactPressed = true;
        const Outcome out = Press(scene, both);
        Check(out.use && out.reloadAfterFailedUse, "B alongside Interact keeps B's combined behavior");
    }
}

void TestModernRequestsAreGatedLikeB() {
    MoveInput request;
    request.interactPressed = true;
    request.reloadPressed = true;

    const char* names[] = { "controls are locked", "actions are disabled", "the game is paused",
                            "a menu is open",      "the player is dead",   "the pad is stopped" };
    for (int gate = 0; gate < 6; gate++) {
        Scene scene;
        scene.context.controlsLocked = gate == 0;
        scene.context.actionsDisabled = gate == 1;
        scene.context.paused = gate == 2;
        scene.context.menuOn = gate == 3;
        scene.context.dead = gate == 4;
        scene.context.padStopped = gate == 5;
        const Outcome out = Press(scene, request);
        const std::string what = std::string("Interact and Reload are dropped while ") + names[gate];
        Check(!out.use && !out.reload, what.c_str());
    }

    Scene watched;
    FakeWatch watch;
    watch.state = 5;
    watched.context.watch = &watch;
    const Outcome out = Press(watched, request);
    Check(!out.use && !out.reload, "Interact and Reload are dropped while the watch is up");

    // Each is an edge the source sends once: a tick without it asks for nothing.
    Scene scene;
    Check(Press(scene, request).reload && !Press(scene, {}).reload, "a Reload request lasts the tick it is sent");
}

void TestTankInteract() {
    FakeTankHooks hooks;
    Scene scene;
    scene.context.tank = &hooks;
    PlayerMoveTestAccess::SetWorldTank(scene.player, 0);
    // The player climbs onto the tank, then may enter it.
    scene.Tick({}, 20);

    MoveInput reload;
    reload.reloadPressed = true;
    Check(!Press(scene, reload).use && hooks.entered == 0, "Reload does not enter a tank");

    MoveInput interact;
    interact.interactPressed = true;
    const Outcome out = Press(scene, interact);
    Check(hooks.entered == 1 && scene.player.InTank() && !out.use, "Interact enters the tank, not a prop use");

    scene.Tick({}, 2);
    Check(!Press(scene, reload).reload, "Reload does nothing in a tank");
    Check(Press(scene, interact).reload == false && hooks.left == 1 && !scene.player.InTank(),
          "Interact leaves the tank");
}

// Items 0..29 where 29 is the remote mine; every item carries ammo type 1.
std::string MineTable() {
    std::string text;
    for (int i = 0; i < 30; i++) {
        text += "item\t" + std::to_string(i) + "\tITEM\t1\t" + std::string(i == 0 ? "0" : "1") +
                "\t7\t0x40800\t-\t-\t1.0\t11.0\t-20.0\t-33.0\t3.0\t3.0\t8.5\t0.8"
                "\t0.9\t1.0\t0.0\t-1\t0\t3\t5\t0\t-1\t2.0\t4.0\t-\t0.0\t10.0\t4.0\t2.0\t4.0\t0.0\t0.0\t0\t1.0\t0\t0\n";
    }
    text += "ammo\t0\tAMMO_NONE\t0\t0x0\t0.0\nammo\t1\tAMMO_9MM\t800\t0x0\t0.0\n";
    return text;
}

void TestRemoteMineChordIgnoresModernInputs() {
    Inventory inventory;
    inventory.AddItem(kItemRemoteMine);
    PlayerWeapons weapons;
    weapons.Init(WeaponTable::Parse(MineTable()), &inventory, [] { return 0u; });
    weapons.EquipWeapon(HandRight, kItemRemoteMine);
    GunTickInput raise;
    raise.random = [] { return 0u; };
    for (int i = 0; i < 60; i++) {
        weapons.TickGameplay(raise, nullptr);
    }

    {
        Scene scene;
        scene.context.weapons = &weapons;
        MoveInput chord;
        chord.buttons = Buttons::A | Buttons::B;
        scene.Tick(chord);
        Check(scene.player.LastMove().detonating, "A+B detonates with the remote mine out");
    }
    {
        Scene scene;
        scene.context.weapons = &weapons;
        MoveInput modern;
        modern.buttons = Buttons::A;
        modern.interactPressed = true;
        modern.reloadPressed = true;
        scene.Tick(modern);
        Check(!scene.player.LastMove().detonating, "A with Interact and Reload is not the chord");
        Check(scene.player.UsePressed() && !scene.player.UseFallsBackToReload() && scene.player.ReloadPressed(),
              "and both Modern requests still reach the stage");
    }
    {
        // The Modern chord is A+B as buttons, which takes the tick from Interact.
        Scene scene;
        scene.context.weapons = &weapons;
        MoveInput chord;
        chord.buttons = Buttons::A | Buttons::B;
        chord.interactPressed = true;
        chord.reloadPressed = true;
        scene.Tick(chord);
        Check(scene.player.LastMove().detonating && !scene.player.UsePressed() && !scene.player.ReloadPressed(),
              "a detonation tick uses and reloads nothing");
    }
}

void TestReloadHonorsTheGunGuards() {
    Inventory inventory;
    inventory.AddItem(4);
    PlayerWeapons weapons;
    weapons.Init(WeaponTable::Parse(MineTable()), &inventory, [] { return 0u; });
    weapons.GiveAmmo(1, 100);
    weapons.EquipWeapon(HandRight, 4);
    weapons.MutableHand(HandRight).modelShown = true;
    GunTickInput gun;
    gun.random = [] { return 0u; };
    for (int i = 0; i < 60; i++) {
        weapons.TickGameplay(gun, nullptr);
    }

    // The stage's Reload is AttemptIdleReload on each hand.
    weapons.AttemptIdleReload(HandRight);
    Check(weapons.Hands(HandRight).currentAnimation == GunState::ReloadStart, "Reload starts the reload animation");
    weapons.AttemptIdleReload(HandRight);
    Check(weapons.Hands(HandRight).currentAnimation == GunState::ReloadStart, "a second request is the same one");
    for (int i = 0; i < 40; i++) {
        weapons.TickGameplay(gun, nullptr);
    }

    // A shot leaves the hand busy: the decomp's AttemptReload would queue a
    // reload behind it, the Modern one takes nothing.
    gun.triggerOn = true;
    weapons.TickGameplay(gun, nullptr);
    Check(weapons.Hands(HandRight).actionState != GunState::Idle, "the shot keeps the hand busy");
    weapons.AttemptIdleReload(HandRight);
    Check(weapons.Hands(HandRight).currentAnimation == 0, "Reload while the gun is busy queues nothing");

    // The unarmed hand has no ammo type, so Reload leaves it alone.
    weapons.AttemptIdleReload(HandLeft);
    Check(weapons.Hands(HandLeft).currentAnimation == 0, "the unarmed hand does not reload");
}

// Crouch as its own action (MoveInput::crouchMode).
MoveInput CrouchInput(CrouchMode mode, bool held) {
    MoveInput input;
    input.crouchMode = mode;
    input.crouchHeld = held;
    return input;
}

bool Crouched(const Scene& scene) {
    return scene.player.CrouchPosition() == kCrouchSquat;
}

void TestStandaloneCrouchHold() {
    Scene scene;
    scene.Tick(CrouchInput(CrouchMode::Hold, true), 40);
    Check(Crouched(scene) && !scene.player.InSightAim(), "Hold crouches outside aim");
    Check(Near(scene.player.DuckingHeightOffset(), -100.0f), "and the view drops to the crouch height");
    scene.Tick(CrouchInput(CrouchMode::Hold, false), 40);
    Check(!Crouched(scene) && Near(scene.player.DuckingHeightOffset(), 0.0f), "releasing the key stands up");

    // Aim is neither needed nor in the way, and the control style's aim-crouch
    // input is ignored: 1.x would stand the player up with Up not held.
    MoveInput aimCrouch = CrouchInput(CrouchMode::Hold, true);
    aimCrouch.buttons = Buttons::L;
    scene.Tick(aimCrouch, 40);
    Check(scene.player.InSightAim() && Crouched(scene), "aiming while holding crouch stays crouched");
    aimCrouch.crouchHeld = false;
    aimCrouch.buttons = Buttons::L | Buttons::DownC;
    scene.Tick(aimCrouch, 40);
    Check(!Crouched(scene), "the style's aim-crouch button does not crouch in Modern");
}

void TestStandaloneCrouchToggle() {
    Scene scene;
    scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(Crouched(scene), "a press crouches");
    scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
    Check(Crouched(scene), "releasing the key keeps the crouch");
    scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(!Crouched(scene), "the next press stands up, and holding it flips only once");
    scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
    Check(!Crouched(scene), "still standing");

    // A toggled crouch persists through aiming and back.
    scene.Tick(CrouchInput(CrouchMode::Toggle, true));
    scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
    MoveInput aim = CrouchInput(CrouchMode::Toggle, false);
    aim.buttons = Buttons::L;
    scene.Tick(aim, 20);
    aim.buttons = 0;
    scene.Tick(aim, 20);
    Check(Crouched(scene), "aiming and releasing aim keeps a toggled crouch");
}

void TestStandaloneCrouchGates() {
    const char* names[] = { "controls are locked", "actions are disabled", "the player is dead", "the pad is stopped",
                            "a scoped gun is out" };
    for (const CrouchMode mode : { CrouchMode::Hold, CrouchMode::Toggle }) {
        for (int gate = 0; gate < 5; gate++) {
            Scene scene;
            scene.context.controlsLocked = gate == 0;
            scene.context.actionsDisabled = gate == 1;
            scene.context.dead = gate == 2;
            scene.context.padStopped = gate == 3;
            scene.context.weaponDisablesCrouch = gate == 4;
            scene.Tick(CrouchInput(mode, true), 40);
            const std::string what = std::string("crouch is ignored while ") + names[gate];
            Check(!Crouched(scene), what.c_str());
        }
    }

    // The watch up: the press is not read, and an edge made there is not kept.
    Scene watched;
    FakeWatch watch;
    watch.state = 5;
    watched.context.watch = &watch;
    watched.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(!Crouched(watched), "crouch is ignored while the watch is up");
    watch.state = 0;
    watched.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(!Crouched(watched), "a key held through the watch closing is not a new press");
    watched.Tick(CrouchInput(CrouchMode::Toggle, false));
    watched.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(Crouched(watched), "and a fresh press crouches");

    // A crouch stays put while the player cannot act, as in the decomp.
    Scene held;
    held.Tick(CrouchInput(CrouchMode::Hold, true), 40);
    held.context.controlsLocked = true;
    held.Tick(CrouchInput(CrouchMode::Hold, false), 40);
    Check(Crouched(held), "locked controls leave the crouch as it was");
}

void TestCrouchToggleResets() {
    {
        Scene scene;
        scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
        scene.Tick(CrouchInput(CrouchMode::Toggle, false));
        scene.context.dead = true;
        scene.Tick(CrouchInput(CrouchMode::Toggle, false));
        scene.context.dead = false;
        scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
        Check(!Crouched(scene), "death clears the toggle: the player stands once alive again");
    }
    {
        Scene scene;
        scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
        scene.Tick(CrouchInput(CrouchMode::Toggle, false));
        scene.context.weaponDisablesCrouch = true;
        scene.Tick(CrouchInput(CrouchMode::Toggle, false));
        scene.context.weaponDisablesCrouch = false;
        scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
        Check(!Crouched(scene), "a scoped gun clears the toggle, so the player stands once crouch is allowed again");
    }
    {
        Scene scene;
        scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
        scene.Tick(CrouchInput(CrouchMode::Hold, false), 40);
        Check(!Crouched(scene), "leaving Toggle mode clears the toggle");
        scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
        Check(!Crouched(scene), "and it stays clear");
    }
    {
        Scene scene;
        scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
        PlayerSpawn spawn;
        spawn.position = { 500.0f, 1.0f, 800.0f };
        spawn.look = { 0.0f, 0.0f, 1.0f };
        spawn.stanTile = 0;
        spawn.stanHeight = 0.0f;
        spawn.eyeHeight = 175.0f;
        scene.player.Init(spawn, scene.stan);
        scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
        Check(!Crouched(scene), "a new stage starts standing");
        scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
        Check(Crouched(scene), "and the first press crouches");
    }
}

void TestCrouchInATank() {
    FakeTankHooks hooks;
    Scene scene;
    scene.context.tank = &hooks;
    PlayerMoveTestAccess::SetWorldTank(scene.player, 0);
    scene.Tick({}, 20);
    scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    scene.Tick(CrouchInput(CrouchMode::Toggle, false));
    Check(Crouched(scene), "crouched before the tank");
    MoveInput enter = CrouchInput(CrouchMode::Toggle, false);
    enter.interactPressed = true;
    scene.Tick(enter);
    Check(scene.player.InTank() && scene.player.CrouchPosition() == kCrouchHalf, "entering a tank sits half-height");
    scene.Tick(CrouchInput(CrouchMode::Toggle, true), 20);
    scene.Tick(CrouchInput(CrouchMode::Toggle, false), 20);
    Check(scene.player.CrouchPosition() == kCrouchHalf, "crouch does nothing in a tank");
    MoveInput leave = CrouchInput(CrouchMode::Toggle, false);
    leave.interactPressed = true;
    scene.Tick(leave);
    scene.Tick(CrouchInput(CrouchMode::Toggle, false), 40);
    Check(!scene.player.InTank() && !Crouched(scene), "leaving the tank stands, with no toggle left over");
}

void TestUnsetCrouchIsTheDecomp() {
    // With crouchMode None, crouchHeld is ignored and only aim crouches.
    MoveInput held;
    held.crouchHeld = true;
    Scene scene;
    scene.Tick(held, 40);
    Check(!Crouched(scene), "crouchHeld alone does nothing without a mode");
    MoveInput aimCrouch = held;
    aimCrouch.buttons = Buttons::L | Buttons::DownC;
    scene.Tick(aimCrouch, 40);
    Check(Crouched(scene), "the style's aim-crouch still works");
    MoveInput plain;
    scene.Tick(plain, 40);
    Check(Crouched(scene), "and a crouch persists outside aim");
    MoveInput aim;
    aim.buttons = Buttons::L;
    scene.Tick(aim, 40);
    Check(!Crouched(scene), "until aiming without the button stands up");

    // Tick for tick the same as an input that never sets the fields.
    Scene a;
    Scene b;
    bool same = true;
    for (int i = 0; i < 120; i++) {
        MoveInput scripted;
        scripted.buttons = (i % 40 < 20) ? (Buttons::L | Buttons::DownC) : Buttons::L;
        scripted.stickY = (i % 7) * 5;
        MoveInput withFields = scripted;
        withFields.crouchHeld = (i % 3) == 0;
        a.Tick(scripted);
        b.Tick(withFields);
        same = same && a.player.CrouchPosition() == b.player.CrouchPosition() &&
               a.player.DuckingHeightOffset() == b.player.DuckingHeightOffset() &&
               a.player.CollisionPosition()[2] == b.player.CollisionPosition()[2];
    }
    Check(same, "an unset crouch mode changes nothing");
}

// Modern controls (MoveInput::modern).
MoveInput ModernInputOf() {
    MoveInput input;
    input.modern = true;
    return input;
}

void TestModernWalkAndStrafe() {
    Scene scene;
    MoveInput input = ModernInputOf();
    input.moveY = 75;
    scene.Tick(input);
    Check(Near(scene.player.SpeedForwards(), 70.0f / 70.0f * 1.08f, 0.02f) && scene.player.SpeedSideways() == 0.0f,
          "Modern walking reads the move stick through the game's dead zone");
    Check(scene.player.Theta() == 0.0f, "and does not turn");

    Scene strafe;
    input = ModernInputOf();
    input.moveX = 75;
    strafe.Tick(input);
    Check(Near(strafe.player.SpeedSideways(), 1.0f) && strafe.player.Theta() == 0.0f,
          "strafing is the analog sideways speed, with no turn");
    input.moveX = 40;
    strafe.Tick(input);
    Check(Near(strafe.player.SpeedSideways(), 35.0f / 70.0f), "and follows the stick");

    // The pad's stick is the crosshair stick: outside aim it does not walk or turn.
    Scene stick;
    input = ModernInputOf();
    input.stickX = 70;
    input.stickY = 70;
    stick.Tick(input);
    Check(stick.player.SpeedForwards() == 0.0f && stick.player.Theta() == 0.0f,
          "the aim stick does nothing outside sight-aim");
}

void TestModernIgnoresTheControlStyle() {
    Scene scene;
    scene.context.options.controlType = ControlConfig::Plenty;
    MoveInput input = ModernInputOf();
    input.stick2Y = 70;
    input.buttons2 = Buttons::Z;
    scene.Tick(input);
    Check(!scene.player.InSightAim() && scene.player.LastMove().analogPitch == 0,
          "a two-controller style's second pad is not read in Modern");
    Scene n64;
    n64.context.options.controlType = ControlConfig::Plenty;
    input.modern = false;
    n64.Tick(input);
    Check(n64.player.LastMove().analogStrafe != 0 || n64.player.LastMove().analogPitch != 0,
          "and N64 mode still reads it");

    Scene swapped;
    swapped.context.options.controlType = ControlConfig::Kissy;
    MoveInput aim = ModernInputOf();
    aim.buttons = Buttons::L | Buttons::R;
    swapped.Tick(aim);
    Check(swapped.player.InSightAim(), "aim is L and R whatever the style says (1.3 aims with Z)");
}

void TestModernLook() {
    Scene scene;
    MoveInput input = ModernInputOf();
    input.lookYaw = 7.0f;
    scene.Tick(input);
    Check(Near(scene.player.Theta(), 7.0f), "mouse yaw turns the view by those degrees");
    input.lookYaw = -9.0f;
    scene.Tick(input);
    Check(Near(scene.player.Theta(), 358.0f), "and left turns the other way, wrapping");
    scene.Tick(ModernInputOf());
    Check(Near(scene.player.Theta(), 358.0f) && Near(scene.player.SpeedTheta(), 0.0f),
          "with no easing: the view stops when the mouse does");

    Scene pitch;
    input = ModernInputOf();
    input.lookPitch = 5.0f;
    const float before = pitch.player.Verta();
    pitch.Tick(input);
    Check(Near(pitch.player.Verta(), before + 5.0f) && pitch.player.AppliedView()[1] > 0.0f,
          "looking up raises verta by the degrees and tilts the view up");
    input.lookPitch = 60.0f;
    pitch.Tick(input);
    pitch.Tick(input);
    Check(Near(pitch.player.Verta(), 90.0f), "pitch is held to the decomp's limits");
    input.lookPitch = -100.0f;
    pitch.Tick(input);
    pitch.Tick(input);
    Check(Near(pitch.player.Verta(), -90.0f), "both ways");

    // A rate is the decomp's full-stick speed per tick and scales with the tick time.
    Scene rate;
    input = ModernInputOf();
    input.lookYawRate = 1.0f;
    rate.Tick(input);
    Check(Near(rate.player.Theta(), 3.5f), "a full right stick turns 3.5 degrees a tick, like the left stick did");
    Scene slow;
    slow.context.globalTimerDelta = 2.0f;
    slow.Tick(input);
    Check(Near(slow.player.Theta(), 7.0f), "twice as long a tick turns twice as far");
    input.lookYawRate = 0.0f;
    input.lookYaw = 7.0f;
    Scene fixed;
    fixed.context.globalTimerDelta = 2.0f;
    fixed.Tick(input);
    Check(Near(fixed.player.Theta(), 7.0f), "while a mouse delta is the same whatever the tick time");

    // Zoomed in the view moves less, as the stick's turn does.
    Scene zoomed;
    zoomed.context.fovY = 30.0f;
    zoomed.Tick(input);
    Check(Near(zoomed.player.Theta(), 3.5f), "look scales with the field of view");

    // The slope-following pitch never fights the mouse.
    Scene walking(5);
    MoveInput walk = ModernInputOf();
    walk.moveY = 75;
    walking.Tick(walk, 60);
    Check(Near(walking.player.Verta(), -4.0f), "Modern walking does not look ahead with the slope");
}

void TestModernLookIsGatedLikeTheStick() {
    for (int gate = 0; gate < 3; gate++) {
        Scene scene;
        scene.context.controlsLocked = gate == 0;
        scene.context.actionsDisabled = gate == 1;
        scene.context.dead = gate == 2;
        MoveInput input = ModernInputOf();
        input.lookYaw = 10.0f;
        input.lookPitch = 10.0f;
        input.moveY = 75;
        input.moveX = 75;
        scene.Tick(input);
        Check(Near(scene.player.Theta(), 0.0f) && Near(scene.player.Verta(), -4.0f) &&
                  scene.player.SpeedForwards() == 0.0f && scene.player.SpeedSideways() == 0.0f,
              "locked, disabled and dead players neither look nor walk in Modern");
    }
    Scene watched;
    FakeWatch watch;
    watch.state = 5;
    watched.context.watch = &watch;
    MoveInput input = ModernInputOf();
    input.lookYaw = 10.0f;
    watched.Tick(input);
    Check(Near(watched.player.Theta(), 0.0f), "and the watch up stops the look");
}

void TestModernAimKeepsTheDecompsRules() {
    Scene scene;
    MoveInput aim = ModernInputOf();
    aim.buttons = Buttons::L | Buttons::R;
    aim.moveY = 75;
    aim.moveX = 75;
    aim.lookYaw = 10.0f;
    aim.lookPitch = 10.0f;
    scene.Tick(aim, 3);
    Check(scene.player.InSightAim(), "L and R aim");
    Check(scene.player.SpeedForwards() == 0.0f && scene.player.SpeedSideways() == 0.0f,
          "no walking or strafing while aiming, as in the decomp");
    Check(Near(scene.player.Theta(), 0.0f) && Near(scene.player.Verta(), -4.0f),
          "and the mouse delta does not turn the view: it moves the crosshair stick instead");

    // The crosshair stick past the threshold scrolls the view at the decomp's rate.
    aim.lookYaw = 0.0f;
    aim.lookPitch = 0.0f;
    aim.stickX = 80;
    scene.Tick(aim, 3);
    Check(scene.player.Theta() > 0.0f && scene.player.LastMove().aimTurnRightSpeed > 0.0f,
          "a crosshair at the edge turns the view");
    Scene n64;
    MoveInput stick;
    stick.buttons = Buttons::L | Buttons::R;
    stick.stickX = 80;
    n64.Tick(stick, 3);
    Check(Near(n64.player.Theta(), scene.player.Theta()), "exactly as the stick does in N64 mode");

    // Stick up (mouse up) raises the crosshair and the view in Modern; N64 mode
    // with the option off keeps the decomp's direction.
    Scene up;
    MoveInput modernUp = ModernInputOf();
    modernUp.buttons = Buttons::L | Buttons::R;
    modernUp.stickY = 80;
    up.Tick(modernUp, 3);
    Check(up.player.LastMove().controlStickYRaw < 0 && up.player.Verta() > -4.0f,
          "in Modern, stick up moves the crosshair up and scrolls the view up");
    Scene decompUp;
    MoveInput n64Up;
    n64Up.buttons = Buttons::L | Buttons::R;
    n64Up.stickY = 80;
    decompUp.Tick(n64Up, 3);
    Check(decompUp.player.LastMove().controlStickYRaw > 0 && decompUp.player.Verta() < -4.0f,
          "and N64 mode keeps the decomp's direction");
}

void TestModernDetonateNeedsAMine() {
    Inventory inventory;
    inventory.AddItem(kItemRemoteMine);
    PlayerWeapons mine;
    mine.Init(WeaponTable::Parse(MineTable()), &inventory, [] { return 0u; });
    mine.EquipWeapon(HandRight, kItemRemoteMine);
    GunTickInput raise;
    raise.random = [] { return 0u; };
    for (int i = 0; i < 60; i++) {
        mine.TickGameplay(raise, nullptr);
    }
    MoveInput chord = ModernInputOf();
    chord.buttons = Buttons::A | Buttons::B;
    chord.detonate = true;
    {
        Scene scene;
        scene.context.weapons = &mine;
        scene.Tick(chord);
        Check(scene.player.LastMove().detonating && !scene.player.UsePressed(),
              "the Modern chord detonates with the remote mine out");
    }
    {
        Inventory other;
        other.AddItem(4);
        other.AddItem(5);
        PlayerWeapons gun;
        gun.Init(WeaponTable::Parse(MineTable()), &other, [] { return 0u; });
        gun.EquipWeapon(HandRight, 4);
        for (int i = 0; i < 60; i++) {
            gun.TickGameplay(raise, nullptr);
        }
        Scene scene;
        scene.context.weapons = &gun;
        scene.Tick(chord);
        const MoveData& move = scene.player.LastMove();
        Check(!move.detonating && !move.weaponForwardOffset && !move.weaponBackOffset && !scene.player.UsePressed(),
              "without a mine the chord changes no weapon and uses nothing");
    }
}

void TestModernWeaponChange() {
    // The decomp's reading of A and Z, as the synthesis sends them.
    Scene scene;
    MoveInput forward = ModernInputOf();
    forward.buttons = Buttons::A;
    scene.Tick(forward);
    Check(scene.player.LastMove().weaponForwardOffset && !scene.player.LastMove().weaponBackOffset,
          "A is the next weapon");
    scene.Tick(ModernInputOf());
    MoveInput back = ModernInputOf();
    back.buttons = Buttons::A | Buttons::Z;
    scene.Tick(back);
    Check(scene.player.LastMove().weaponBackOffset && !scene.player.LastMove().triggerOn,
          "A with Z is the previous weapon and does not fire");
    scene.Tick(ModernInputOf());
    MoveInput hold = ModernInputOf();
    hold.buttons = Buttons::A;
    scene.Tick(hold);
    hold.buttons = Buttons::A | Buttons::Z;
    scene.Tick(hold);
    Check(scene.player.LastMove().weaponBackOffset,
          "holding A and pressing Z (hold North, fire) is the previous weapon");
}

void TestModernLeanAndZoomAreTheDecomps() {
    Scene scene;
    scene.context.weaponDisablesCrouch = true;
    MoveInput input = ModernInputOf();
    input.buttons = Buttons::L | Buttons::R | Buttons::LeftPad;
    scene.Tick(input, 2);
    Check(scene.player.LastMove().rLeanLeft && !scene.player.LastMove().rLeanRight,
          "D-pad left leans left while aiming");
    input.buttons = Buttons::L | Buttons::R | Buttons::UpPad;
    scene.Tick(input);
    Check(scene.player.LastMove().zoomInFovPersec > 0.0f, "D-pad up zooms in with a scoped gun");
    Scene plain;
    plain.Tick(input);
    Check(plain.player.LastMove().zoomInFovPersec == 0.0f, "and not without one");
    Scene lean;
    MoveInput off = ModernInputOf();
    off.buttons = Buttons::LeftPad;
    lean.Tick(off);
    Check(!lean.player.LastMove().rLeanLeft, "lean is aim only");
}

void TestModernMovementNeedsNothingOfTheStyleButtons() {
    // A Modern port never sends B except as the detonate chord, and B alone is not an interact.
    Scene scene;
    MoveInput input = ModernInputOf();
    input.buttons = Buttons::B;
    Check(!Press(scene, input).use, "B is not an interact in Modern");
}

void TestModernTank() {
    FakeTankHooks hooks;
    Scene scene;
    scene.context.tank = &hooks;
    PlayerMoveTestAccess::SetWorldTank(scene.player, 0);
    scene.Tick({}, 20);
    MoveInput interact = ModernInputOf();
    interact.interactPressed = true;
    scene.Tick(interact);
    scene.Tick(ModernInputOf(), 60);
    Check(scene.player.InTank(), "the player is in the tank");

    MoveInput input = ModernInputOf();
    input.moveX = 75;
    input.moveY = 75;
    scene.Tick(input);
    Check(scene.player.LastMove().analogTurn == 70 && scene.player.LastMove().analogStrafe == 0 &&
              scene.player.LastMove().analogWalk == 70,
          "in a tank the move stick drives and turns the hull");

    input = ModernInputOf();
    input.lookYaw = 6.0f;
    scene.Tick(input);
    Check(Near(PlayerMoveTestAccess::TankTurretTurn(scene.player), 6.0f * 0.017453292f, 1e-4f),
          "and the mouse turns the turret");
}

void TestFrozenTickKeepsTheCrouchKeyLevel() {
    // A key held across a cinematic is not a new press when the player can move again.
    Scene scene;
    scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(Crouched(scene), "a press toggles the crouch on");
    for (int i = 0; i < 5; i++) {
        scene.player.TickFrozen(scene.context, CrouchInput(CrouchMode::Toggle, true), scene.stan);
    }
    scene.Tick(CrouchInput(CrouchMode::Toggle, true), 40);
    Check(Crouched(scene), "the key held through frozen ticks does not toggle it off");
}

void TestUnsetModernChangesNothing() {
    // Tick for tick the same as an input that never sets the Modern fields.
    Scene a;
    Scene b;
    bool same = true;
    for (int i = 0; i < 120; i++) {
        MoveInput scripted;
        scripted.buttons = (i % 40 < 20) ? (Buttons::L | Buttons::LeftC) : Buttons::Z;
        scripted.stickX = (i % 9) * 6 - 20;
        scripted.stickY = (i % 7) * 5;
        MoveInput withFields = scripted;
        withFields.moveX = 40;
        withFields.moveY = 70;
        withFields.lookYaw = 5.0f;
        withFields.lookPitch = 5.0f;
        withFields.lookYawRate = 1.0f;
        withFields.lookPitchRate = 1.0f;
        withFields.detonate = true;
        a.Tick(scripted);
        b.Tick(withFields);
        same = same && a.player.Theta() == b.player.Theta() && a.player.Verta() == b.player.Verta() &&
               a.player.CollisionPosition()[0] == b.player.CollisionPosition()[0] &&
               a.player.CollisionPosition()[2] == b.player.CollisionPosition()[2] &&
               a.player.SpeedForwards() == b.player.SpeedForwards();
    }
    Check(same, "with `modern` unset the other Modern fields change nothing");
}

int main() {
    TestModernWalkAndStrafe();
    TestModernIgnoresTheControlStyle();
    TestModernLook();
    TestModernLookIsGatedLikeTheStick();
    TestModernAimKeepsTheDecompsRules();
    TestModernDetonateNeedsAMine();
    TestModernWeaponChange();
    TestModernLeanAndZoomAreTheDecomps();
    TestModernMovementNeedsNothingOfTheStyleButtons();
    TestModernTank();
    TestFrozenTickKeepsTheCrouchKeyLevel();
    TestUnsetModernChangesNothing();
    TestStandaloneCrouchHold();
    TestStandaloneCrouchToggle();
    TestStandaloneCrouchGates();
    TestCrouchToggleResets();
    TestCrouchInATank();
    TestUnsetCrouchIsTheDecomp();
    TestInteractAndReloadAreSeparate();
    TestModernRequestsAreGatedLikeB();
    TestTankInteract();
    TestRemoteMineChordIgnoresModernInputs();
    TestReloadHonorsTheGunGuards();
    TestPlayerProp();
    TestTheWatchGatesTheControls();
    TestTurnReadsTheZoomReachedThisTick();
    TestOptionsFromSaveBits();
    TestParseControlStyle();
    TestStandingStill();
    TestTurning();
    TestSpawnState();
    TestAppliedView();
    TestWalkSpeed();
    TestHeadMotionMovesThePlayer();
    TestWallStopsThePlayer();
    TestSlidingAlongAWall();
    TestCrossingToTheNextTile();
    TestFalling();
    TestCrouching();
    TestLeaning();
    TestControlStyles();
    TestOneControllerStyles();
    TestTwoControllerStyles();
    TestLookAhead();
    return gFailures == 0 ? 0 : 1;
}
