#include <cmath>
#include <cstdio>

#include "StanFixture.h"
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
    Vec3 Tick(float, float, float, const MoveContext&) override {
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

} // namespace

int main() {
    TestStandingStill();
    TestTurning();
    TestWalkSpeed();
    TestHeadMotionMovesThePlayer();
    TestWallStopsThePlayer();
    TestSlidingAlongAWall();
    TestCrossingToTheNextTile();
    TestFalling();
    TestCrouching();
    TestLeaning();
    TestControlStyles();
    TestLookAhead();
    return gFailures == 0 ? 0 : 1;
}
