#include <cmath>
#include <cstdio>

#include "StanFixture.h"
#include "level/Objects.h"
#include "level/PlayerMove.h"
#include "level/Stan.h"

using namespace Frigate::Level;
using namespace StanFixture;

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
    objects.SetViewerFootprint([&scene](PropCollider* out) { return scene.player.Footprint(out); });
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

int main() {
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
