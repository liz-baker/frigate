#include <cmath>
#include <cstdio>
#include <vector>

#include "platform/ModernInput.h"

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) < tolerance;
}

// libultra's CONT_* masks.
constexpr uint16_t kR = 0x0010;
constexpr uint16_t kL = 0x0020;
constexpr uint16_t kDRight = 0x0100;
constexpr uint16_t kDLeft = 0x0200;
constexpr uint16_t kDDown = 0x0400;
constexpr uint16_t kDUp = 0x0800;
constexpr uint16_t kStart = 0x1000;
constexpr uint16_t kZ = 0x2000;
constexpr uint16_t kB = 0x4000;
constexpr uint16_t kA = 0x8000;

// PS/2 set 1 scancodes.
constexpr int kW = 17;
constexpr int kS = 31;
constexpr int kA_ = 30;
constexpr int kD = 32;
constexpr int kKeyZ = 44;
constexpr int kKeyC = 46;
constexpr int kE = 18;
constexpr int kR_ = 19;
constexpr int kT = 20;
constexpr int kTab = 15;
constexpr int kCtrl = 29;
constexpr int kSpace = 57;

const Profile& Kbm() {
    return DefaultProfile(BindingMode::Modern, ProfileDevice::KeyboardMouse);
}

const Profile& Pad() {
    return DefaultProfile(BindingMode::Modern, ProfileDevice::Gamepad);
}

ModernContext Live() {
    ModernContext context;
    context.menu = false;
    return context;
}

ModernContext Aiming(bool zoomWeapon = false) {
    ModernContext context = Live();
    context.aiming = true;
    context.zoomWeapon = zoomWeapon;
    return context;
}

DeviceFrame Keys(std::vector<PhysicalInput> held) {
    DeviceFrame frame;
    frame.held = std::move(held);
    return frame;
}

DeviceFrame Wheel(WheelDir direction, int notches = 1) {
    DeviceFrame frame;
    frame.wheel[static_cast<size_t>(direction)] = notches;
    return frame;
}

DeviceFrame Axis(PadAxis axis, float value) {
    DeviceFrame frame;
    frame.axes[static_cast<size_t>(axis)] = value;
    return frame;
}

ModernOutput Step(ModernSynth& synth, const Profile& profile, ProfileDevice device, const DeviceFrame& frame,
                  const ModernContext& context, const ModernSettings& settings = {}) {
    return synth.Step(profile, device, frame, context, settings);
}

ModernOutput StepK(ModernSynth& synth, const DeviceFrame& frame, const ModernContext& context,
                   const ModernSettings& settings = {}) {
    return Step(synth, Kbm(), ProfileDevice::KeyboardMouse, frame, context, settings);
}

ModernOutput StepP(ModernSynth& synth, const DeviceFrame& frame, const ModernContext& context,
                   const ModernSettings& settings = {}) {
    return Step(synth, Pad(), ProfileDevice::Gamepad, frame, context, settings);
}

void TestMenuContext() {
    ModernSynth synth;
    ModernOutput out = StepK(synth, Keys({ KeyInput(kW), KeyInput(kSpace) }), ModernContext{});
    Check(out.pad.stickY == kStickMax && out.pad.stickX == 0, "W is stick up in the menu");
    Check((out.pad.buttons & kA) != 0 && (out.pad.buttons & ~kA) == 0, "Space is A and nothing else is read");
    Check(out.frame.moveY == 0 && !out.frame.interactPressed, "no gameplay input reaches the menu");

    out = StepK(synth, Keys({ KeyInput(kTab) }), ModernContext{});
    Check(out.pad.buttons == kStart, "Pause is Start in a menu");
    out = StepK(synth, Keys({ KeyInput(kA_), KeyInput(14) }), ModernContext{});
    Check(out.pad.stickX == -kStickMax && (out.pad.buttons & kB) != 0, "A is left and Backspace is B");

    // Left click selects and right click goes back, with no stick (frigate#501).
    out = StepK(synth, Keys({ MouseInput(0) }), ModernContext{});
    Check((out.pad.buttons & kA) != 0 && out.pad.stickX == 0 && out.pad.stickY == 0, "a left click is A");
    out = StepK(synth, Keys({ MouseInput(2) }), ModernContext{});
    Check((out.pad.buttons & kB) != 0 && (out.pad.buttons & kA) == 0, "a right click is B");
    out = StepK(synth, Keys({ MouseInput(0) }), Live());
    Check((out.pad.buttons & kA) == 0, "a click isn't A in play");

    ModernSynth pad;
    out = StepP(pad, Axis(PadAxis::LeftX, 0.5f), ModernContext{});
    Check(out.pad.stickX == 40, "a stick keeps its analog value for the cursor");
    out = StepP(pad, Keys({ PadInput(PadButton::South) }), ModernContext{});
    Check((out.pad.buttons & kA) != 0, "South selects");
    out = StepP(pad, Keys({ PadInput(PadButton::East) }), ModernContext{});
    Check((out.pad.buttons & kB) != 0, "East goes back");
    out = StepP(pad, Keys({ PadInput(PadButton::Start) }), ModernContext{});
    Check(out.pad.buttons == kStart, "Start pauses");
}

void TestMovement() {
    ModernSynth synth;
    ModernOutput out = StepK(synth, Keys({ KeyInput(kW) }), Live());
    Check(out.frame.moveY == kStickFull && out.frame.moveX == 0, "W walks forward");
    out = StepK(synth, Keys({ KeyInput(kS) }), Live());
    Check(out.frame.moveY == -kStickFull, "S walks back");
    out = StepK(synth, Keys({ KeyInput(kA_) }), Live());
    Check(out.frame.moveX == -kStickFull, "A strafes left");
    out = StepK(synth, Keys({ KeyInput(kD) }), Live());
    Check(out.frame.moveX == kStickFull, "D strafes right");
    out = StepK(synth, Keys({ KeyInput(kW), KeyInput(kS) }), Live());
    Check(out.frame.moveY == 0, "opposite keys cancel");
    out = StepK(synth, Keys({ KeyInput(kW), KeyInput(kD) }), Live());
    Check(out.frame.moveX == out.frame.moveY && out.frame.moveX > 50 && out.frame.moveX < kStickFull,
          "a diagonal is no faster than straight");
    Check(out.pad.stickX == 0 && out.pad.stickY == 0, "the pad stick stays the aim stick, centred");

    out = StepK(synth, Keys({ KeyInput(kW) }), Aiming());
    Check(out.frame.moveY == 0 && out.frame.moveX == 0, "no walking while aiming");

    ModernSynth pad;
    out = StepP(pad, Axis(PadAxis::LeftY, 0.4f), Live());
    Check(out.frame.moveY == 30, "the left stick walks by its value");
    out = StepP(pad, Axis(PadAxis::LeftY, -1.0f), Live());
    Check(out.frame.moveY == -kStickFull, "backwards");
    out = StepP(pad, Axis(PadAxis::LeftX, 1.0f), Live());
    Check(out.frame.moveX == kStickFull, "and strafes");
}

void TestAimAndFire() {
    ModernSynth synth;
    ModernOutput out = StepK(synth, Keys({ MouseInput(2) }), Live());
    Check(out.pad.buttons == (kL | kR), "the right mouse button aims (L and R)");
    out = StepK(synth, Keys({ MouseInput(0) }), Live());
    Check(out.pad.buttons == kZ, "the left mouse button fires (Z)");

    ModernSynth pad;
    out = StepP(pad, Keys({ PadInput(PadButton::LeftShoulder) }), Live());
    Check(out.pad.buttons == (kL | kR), "LB aims");
    out = StepP(pad, Axis(PadAxis::LeftTrigger, 0.8f), Live());
    Check(out.pad.buttons == (kL | kR), "so does a pulled LT");
    out = StepP(pad, Axis(PadAxis::LeftTrigger, 0.3f), Live());
    Check(out.pad.buttons == 0, "a light touch of LT does not");
    out = StepP(pad, Keys({ PadInput(PadButton::RightShoulder) }), Live());
    Check(out.pad.buttons == kZ, "RB fires");
    out = StepP(pad, Axis(PadAxis::RightTrigger, 1.0f), Live());
    Check(out.pad.buttons == kZ, "so does RT");
}

void TestLook() {
    ModernSynth synth;
    DeviceFrame frame;
    frame.mouseDx = 100.0f;
    frame.mouseDy = -50.0f;
    ModernOutput out = StepK(synth, frame, Live());
    Check(Near(out.frame.lookYaw, 100.0f * kMouseDegreesPerPixel) &&
              Near(out.frame.lookPitch, 50.0f * kMouseDegreesPerPixel),
          "mouse right turns right and mouse up looks up, in degrees");
    frame.mouseDx = -20.0f;
    frame.mouseDy = 30.0f;
    out = StepK(synth, frame, Live());
    Check(out.frame.lookYaw < 0.0f && out.frame.lookPitch < 0.0f, "left and down the other way");
    ModernSettings fast;
    fast.mouseSensitivity = 2.0f;
    frame.mouseDx = 10.0f;
    frame.mouseDy = 0.0f;
    out = StepK(synth, frame, Live(), fast);
    Check(Near(out.frame.lookYaw, 10.0f * kMouseDegreesPerPixel * 2.0f), "the mouse sensitivity scales it");
    Check(out.frame.lookYawRate == 0.0f && out.pad.stickX == 0, "and it is not a stick");

    ModernSynth pad;
    out = StepP(pad, Axis(PadAxis::RightX, 0.5f), Live());
    Check(Near(out.frame.lookYawRate, 0.25f) && out.frame.lookYaw == 0.0f,
          "the right stick is a rate through the decomp's squared response");
    ModernSettings slow;
    slow.lookSensitivity = 0.5f;
    out = StepP(pad, Axis(PadAxis::RightY, 1.0f), Live(), slow);
    Check(Near(out.frame.lookPitchRate, 0.5f), "scaled by the look sensitivity, up is up");
    out = StepP(pad, Axis(PadAxis::RightY, -1.0f), Live());
    Check(out.frame.lookPitchRate < 0.0f, "and down is down");
}

void TestAimingMovesTheCrosshair() {
    ModernSynth synth;
    DeviceFrame frame;
    frame.mouseDx = 50.0f;
    ModernOutput out = StepK(synth, frame, Aiming());
    Check(out.pad.stickX == 10 && out.frame.lookYaw == 0.0f, "the mouse moves the crosshair stick while aiming");
    out = StepK(synth, frame, Aiming());
    Check(out.pad.stickX == 20, "and it stays where it was moved");
    frame.mouseDx = 0.0f;
    out = StepK(synth, frame, Aiming());
    Check(out.pad.stickX == 20, "when the mouse stops");
    frame.mouseDy = -100.0f;
    out = StepK(synth, frame, Aiming());
    Check(out.pad.stickY == 20, "up is stick up");
    frame.mouseDx = 2000.0f;
    out = StepK(synth, frame, Aiming());
    Check(out.pad.stickX == kStickMax, "and it stops at the stick's travel");
    frame.mouseDx = 0.0f;
    frame.mouseDy = 0.0f;
    out = StepK(synth, frame, Live());
    Check(out.pad.stickX == 0 && out.pad.stickY == 0, "leaving aim recentres it");
    out = StepK(synth, frame, Aiming());
    Check(out.pad.stickX == 0, "and the next aim starts centred");

    // The aim key goes down a tick before the game reports aiming.
    ModernSynth first;
    DeviceFrame down = Keys({ MouseInput(2), KeyInput(kW) });
    down.mouseDx = 30.0f;
    out = StepK(first, down, Live());
    Check(out.frame.moveY == 0 && out.frame.lookYaw == 0.0f && out.pad.stickX == 6,
          "the tick the aim key goes down already aims");

    ModernSynth pad;
    out = StepP(pad, Axis(PadAxis::RightX, 0.5f), Aiming());
    Check(out.pad.stickX == 40 && out.frame.lookYawRate == 0.0f, "the right stick is the crosshair stick while aiming");
    ModernSettings settings;
    settings.lookSensitivity = 3.0f;
    out = StepP(pad, Axis(PadAxis::RightX, 0.9f), Aiming(), settings);
    Check(out.pad.stickX == kStickMax, "clamped to its travel");
}

void TestActions() {
    ModernSynth synth;
    ModernSettings toggling;
    toggling.crouchToggle = true;
    ModernOutput out = StepK(synth, Keys({ KeyInput(kCtrl) }), Live(), toggling);
    Check(out.frame.crouchHeld && out.frame.crouchToggle, "crouch is the key's level with the port's setting");
    out = StepK(synth, Keys({ KeyInput(kCtrl) }), Aiming());
    Check(out.frame.crouchHeld && !out.frame.crouchToggle, "and works while aiming");
    out = StepK(synth, Keys({}), Live());
    Check(!out.frame.crouchHeld, "released");

    out = StepK(synth, Keys({ KeyInput(kE) }), Live());
    Check(out.frame.interactPressed && !out.frame.reloadPressed, "E interacts, on the edge");
    out = StepK(synth, Keys({ KeyInput(kE) }), Live());
    Check(!out.frame.interactPressed, "and only once while held");
    out = StepK(synth, Keys({ KeyInput(kR_) }), Live());
    Check(out.frame.reloadPressed && !out.frame.interactPressed, "R reloads");
    out = StepK(synth, Keys({ KeyInput(kE), KeyInput(kR_) }), Live());
    Check(out.frame.interactPressed && !out.frame.reloadPressed, "both at once are each their own edge");

    ModernSynth menu;
    StepK(menu, Keys({ KeyInput(kE) }), ModernContext{});
    out = StepK(menu, Keys({ KeyInput(kE) }), Live());
    Check(!out.frame.interactPressed, "a key held as the menu closes is not a press");

    ModernSynth pad;
    out = StepP(pad, Keys({ PadInput(PadButton::South) }), Live());
    Check(out.frame.interactPressed, "South interacts");
    out = StepP(pad, Keys({ PadInput(PadButton::West) }), Live());
    Check(out.frame.reloadPressed, "West reloads");

    out = StepK(synth, Keys({ KeyInput(kT) }), Live());
    Check(out.frame.detonate && out.pad.buttons == (kA | kB), "T presses the A+B chord");
    ModernSynth stick;
    out = StepP(stick, Keys({ PadInput(PadButton::RightStick) }), Live());
    Check(out.frame.detonate && out.pad.buttons == (kA | kB), "R3 does");
    out = StepK(synth, Keys({ KeyInput(kTab) }), Live());
    Check(out.pad.buttons == kStart, "Pause is Start in play");
}

void TestWeaponChange() {
    ModernSynth synth;
    // Down is next in the default bindings.
    ModernOutput out = StepK(synth, Wheel(WheelDir::Down), Live());
    Check(out.pad.buttons == kA, "a notch down presses A: next weapon");
    out = StepK(synth, Keys({}), Live());
    Check(out.pad.buttons == 0, "released the frame after");
    out = StepK(synth, Wheel(WheelDir::Up), Live());
    Check(out.pad.buttons == (kA | kZ), "a notch up is A with Z: previous weapon");
    out = StepK(synth, Keys({}), Live());
    Check(out.pad.buttons == 0, "and released again");

    out = StepK(synth, Wheel(WheelDir::Down, 3), Live());
    Check(out.pad.buttons == kA, "a spin is queued: the first");
    out = StepK(synth, Keys({}), Live());
    Check(out.pad.buttons == 0, "a gap");
    out = StepK(synth, Keys({}), Live());
    Check(out.pad.buttons == kA, "the second");
    out = StepK(synth, Keys({}), Live());
    out = StepK(synth, Keys({}), Live());
    Check(out.pad.buttons == kA, "the third");
    out = StepK(synth, Keys({}), Live());
    out = StepK(synth, Keys({}), Live());
    Check(out.pad.buttons == 0, "and no more");

    // North held is A held: fire pressed after it is the decomp's previous weapon.
    ModernSynth pad;
    out = StepP(pad, Keys({ PadInput(PadButton::North) }), Live());
    Check(out.pad.buttons == kA, "North is A");
    out = StepP(pad, Keys({ PadInput(PadButton::North), PadInput(PadButton::RightShoulder) }), Live());
    Check(out.pad.buttons == (kA | kZ), "North held with fire is A with Z");

    // With a scope aimed the wheel zooms instead.
    ModernSynth scoped;
    out = StepK(scoped, Wheel(WheelDir::Up), Aiming(true));
    Check((out.pad.buttons & kDUp) != 0 && (out.pad.buttons & kA) == 0, "the wheel zooms in while aiming a scope");
    for (int i = 0; i < kZoomFramesPerNotch - 1; i++) {
        out = StepK(scoped, Keys({}), Aiming(true));
        Check((out.pad.buttons & kDUp) != 0, "for a few frames per notch");
    }
    out = StepK(scoped, Keys({}), Aiming(true));
    Check(out.pad.buttons == 0, "then stops");
    out = StepK(scoped, Wheel(WheelDir::Down), Aiming(true));
    Check((out.pad.buttons & kDDown) != 0 && (out.pad.buttons & kA) == 0, "and out the other way");

    // A hard spin only queues a few notches, and a notch the other way takes
    // over at once instead of waiting behind them.
    ModernSynth spun;
    for (int i = 0; i < 10; i++) {
        out = StepK(spun, Wheel(WheelDir::Up), Aiming(true));
    }
    int zoomingIn = 0;
    while ((out.pad.buttons & kDUp) != 0 && zoomingIn < 1000) {
        zoomingIn++;
        out = StepK(spun, Keys({}), Aiming(true));
    }
    Check(zoomingIn <= kMaxQueuedZoomFrames, "a spin of the wheel zooms for a bounded time");
    ModernSynth reversed;
    for (int i = 0; i < 10; i++) {
        out = StepK(reversed, Wheel(WheelDir::Up), Aiming(true));
    }
    out = StepK(reversed, Wheel(WheelDir::Down), Aiming(true));
    Check((out.pad.buttons & kDDown) != 0 && (out.pad.buttons & kDUp) == 0,
          "and a notch the other way zooms out straight away");
    ModernSynth plain;
    out = StepK(plain, Wheel(WheelDir::Down), Aiming(false));
    Check(out.pad.buttons == kA, "but changes weapon when the gun has no scope");

    // The left stick zooms with a scope aimed, instead of walking.
    ModernSynth stick;
    out = StepP(stick, Axis(PadAxis::LeftY, 0.9f), Aiming(true));
    Check((out.pad.buttons & kDUp) != 0 && out.frame.moveY == 0, "left stick up zooms in");
    out = StepP(stick, Axis(PadAxis::LeftY, -0.9f), Aiming(true));
    Check((out.pad.buttons & kDDown) != 0, "down zooms out");
    out = StepP(stick, Axis(PadAxis::LeftY, 0.9f), Aiming(false));
    Check((out.pad.buttons & (kDUp | kDDown)) == 0 && out.frame.moveY == 0, "and does nothing without a scope");
    out = StepP(stick, Axis(PadAxis::LeftY, 0.9f), Live());
    Check((out.pad.buttons & (kDUp | kDDown)) == 0 && out.frame.moveY > 0, "or on foot, where it walks");
}

void TestLean() {
    ModernSynth synth;
    ModernOutput out = StepK(synth, Keys({ KeyInput(kKeyZ) }), Aiming());
    Check(out.pad.buttons == kDLeft, "Z leans left while aiming");
    out = StepK(synth, Keys({ KeyInput(kKeyC) }), Aiming());
    Check(out.pad.buttons == kDRight, "C leans right");
    out = StepK(synth, Keys({ KeyInput(kKeyZ) }), Live());
    Check(out.pad.buttons == 0, "lean is aim only");
    ModernContext tank = Aiming();
    tank.inTank = true;
    out = StepK(synth, Keys({ KeyInput(kKeyZ) }), tank);
    Check(out.pad.buttons == 0, "and does not turn a tank's turret");

    ModernSynth pad;
    out = StepP(pad, Keys({ PadInput(PadButton::DpadLeft) }), Aiming());
    Check(out.pad.buttons == kDLeft, "D-pad left leans");
    out = StepP(pad, Keys({ PadInput(PadButton::DpadLeft) }), Live());
    Check(out.pad.buttons == 0, "only while aiming");
}

void TestRebinding() {
    Profile profile = Kbm();
    profile.Of(Action::Fire) = { KeyInput(33) };
    profile.Of(Action::Crouch).clear();
    ModernSynth synth;
    ModernOutput out = Step(synth, profile, ProfileDevice::KeyboardMouse, Keys({ KeyInput(33) }), Live());
    Check(out.pad.buttons == kZ, "a rebound action follows its binding");
    out = Step(synth, profile, ProfileDevice::KeyboardMouse, Keys({ MouseInput(0), KeyInput(kCtrl) }), Live());
    Check(out.pad.buttons == 0 && !out.frame.crouchHeld, "the old input and an unbound action do nothing");

    profile.Of(Action::TurnRight) = { KeyInput(kD + 1) };
    out = Step(synth, profile, ProfileDevice::KeyboardMouse, Keys({ KeyInput(kD + 1) }), Live());
    Check(out.frame.lookYawRate > 0.9f, "a key bound to look turns at the full rate");
}

void TestStateAcrossPorts() {
    ModernSynth a;
    ModernSynth b;
    DeviceFrame frame;
    frame.mouseDx = 50.0f;
    StepK(a, frame, Aiming());
    const ModernOutput other = StepK(b, frame, Aiming());
    Check(other.pad.stickX == 10, "each port's synthesis keeps its own crosshair");
    a.Reset();
    Check(StepK(a, DeviceFrame{}, Aiming()).pad.stickX == 0, "and Reset recentres it");
}

} // namespace

int main() {
    TestMenuContext();
    TestMovement();
    TestAimAndFire();
    TestLook();
    TestAimingMovesTheCrosshair();
    TestActions();
    TestWeaponChange();
    TestLean();
    TestRebinding();
    TestStateAcrossPorts();
    return gFailures == 0 ? 0 : 1;
}
