#include <cstdio>
#include <string>
#include <vector>

#include "platform/BindingCapture.h"
#include "platform/Bindings.h"

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr BindTarget kN64Pad{ 0, BindingMode::N64, ProfileDevice::Gamepad };
constexpr BindTarget kN64Kbm{ 0, BindingMode::N64, ProfileDevice::KeyboardMouse };
constexpr BindTarget kModernKbm{ 0, BindingMode::Modern, ProfileDevice::KeyboardMouse };
constexpr BindTarget kModernPad{ 0, BindingMode::Modern, ProfileDevice::Gamepad };

const ConflictChoice kSwap = ConflictChoice::Swap;
const ConflictChoice kReplace = ConflictChoice::Replace;
const ConflictChoice kCancel = ConflictChoice::Cancel;

bool Has(const Profile& profile, Action action, const PhysicalInput& input) {
    for (const PhysicalInput& bound : profile.Of(action)) {
        if (bound == input) {
            return true;
        }
    }
    return false;
}

void TestDefaults() {
    for (const BindingMode mode : { BindingMode::Modern, BindingMode::N64 }) {
        for (const ProfileDevice device : { ProfileDevice::KeyboardMouse, ProfileDevice::Gamepad }) {
            const Profile& defaults = DefaultProfile(mode, device);
            for (const Action action : ActionsOf(mode)) {
                for (const PhysicalInput& input : defaults.Of(action)) {
                    Check(Accepts(action, input, device), "a default is an input its action accepts");
                    Check(!IsReserved(input), "no default is a reserved input");
                    for (const Action other : ActionsOf(mode)) {
                        if (other == action || (Info(action).contexts & Info(other).contexts) == 0) {
                            continue;
                        }
                        Check(!Has(defaults, other, input), "the defaults don't conflict with each other");
                    }
                }
                if (Info(action).required) {
                    Check(!defaults.Of(action).empty(), "pause, menu navigation and the N64 essentials ship bound");
                }
            }
        }
    }
    Check(ActionsOf(BindingMode::N64).size() == 18, "eighteen N64 actions");
    Check(DefaultProfile(BindingMode::N64, ProfileDevice::KeyboardMouse).Of(Action::MoveForward).empty(),
          "a profile holds only its own mode's actions");
    Check(kDefaultMode == BindingMode::Modern, "Modern is the starting mode");

    // The Modern defaults follow the issue's action table.
    const Profile& kbm = DefaultProfile(BindingMode::Modern, ProfileDevice::KeyboardMouse);
    Check(Has(kbm, Action::Fire, MouseInput(0)) && Has(kbm, Action::Aim, MouseInput(2)), "fire and aim on the mouse");
    Check(Has(kbm, Action::Pause, KeyInput(15)), "pause on Tab");
    const Profile& pad = DefaultProfile(BindingMode::Modern, ProfileDevice::Gamepad);
    Check(Has(pad, Action::Interact, PadInput(PadButton::South)) && Has(pad, Action::Reload, PadInput(PadButton::West)),
          "interact on South and reload on West");
    Check(Has(kbm, Action::Interact, KeyInput(18)) && Has(kbm, Action::Reload, KeyInput(19)),
          "interact on E and reload on R");
    Check(Has(pad, Action::Aim, PadInput(PadButton::LeftShoulder)) &&
              Has(pad, Action::Aim, AxisInput(PadAxis::LeftTrigger, 1)),
          "aim on LB and LT");
}

void TestLabels() {
    const struct {
        PadButton button;
        const char* labels[3];
    } faces[] = {
        { PadButton::North, { "Y", "X", "Triangle" } },
        { PadButton::South, { "A", "B", "Cross" } },
        { PadButton::East, { "B", "A", "Circle" } },
        { PadButton::West, { "X", "Y", "Square" } },
    };
    for (const auto& face : faces) {
        Check(FaceButtonLabel(face.button, FaceLayout::Xbox) == face.labels[0], "Xbox face label");
        Check(FaceButtonLabel(face.button, FaceLayout::Nintendo) == face.labels[1], "Nintendo face label");
        Check(FaceButtonLabel(face.button, FaceLayout::PlayStation) == face.labels[2], "PlayStation face label");
    }
    Check(InputLabel(PadInput(PadButton::South), FaceLayout::Nintendo) == "B", "an input shows its layout's label");
    Check(InputLabel(KeyInput(17), FaceLayout::Xbox) == "W", "keys have names");
    Check(InputLabel(AxisInput(PadAxis::RightTrigger, 1), FaceLayout::PlayStation) == "R2",
          "triggers follow the layout");
    Check(InputLabel(WheelInput(WheelDir::Up), FaceLayout::Xbox) == "Wheel Up", "the wheel is named");
}

void TestProfilesStaySeparate() {
    BindingStore store;
    Check(store.Bind(kN64Pad, Action::N64Z, PadInput(PadButton::RightStick), kNewSlot) == BindStatus::Bound,
          "bind a free input");
    Check(Has(store.Get(kN64Pad), Action::N64Z, PadInput(PadButton::RightStick)), "the edit lands");
    Check(store.Get(kN64Kbm) == DefaultProfile(BindingMode::N64, ProfileDevice::KeyboardMouse),
          "the keyboard profile is untouched by a gamepad edit");
    Check(store.Get(kModernPad) == DefaultProfile(BindingMode::Modern, ProfileDevice::Gamepad),
          "the Modern profile is untouched by an N64 edit");

    BindTarget otherPort = kN64Pad;
    otherPort.port = 1;
    Check(store.Get(otherPort) == DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad), "ports are separate");

    // Switching mode and back keeps both.
    store.SetMode(0, BindingMode::Modern);
    store.Bind(kModernKbm, Action::Detonate, KeyInput(34), kNewSlot);
    store.SetMode(0, BindingMode::N64);
    Check(Has(store.Get(kN64Pad), Action::N64Z, PadInput(PadButton::RightStick)), "N64 edit survives the mode switch");
    Check(Has(store.Get(kModernKbm), Action::Detonate, KeyInput(34)), "Modern edit survives the mode switch");
}

void TestValidity() {
    BindingStore store;
    Check(store.Bind(kN64Kbm, Action::N64A, PadInput(PadButton::South), kNewSlot) == BindStatus::Invalid,
          "a gamepad input can't go on the keyboard profile");
    Check(store.Bind(kN64Pad, Action::N64A, KeyInput(17), kNewSlot) == BindStatus::Invalid,
          "a key can't go on the gamepad profile");
    Check(store.Bind(kModernKbm, Action::N64A, KeyInput(17), kNewSlot) == BindStatus::Invalid,
          "an N64 action isn't in a Modern profile");
    Check(store.Bind(kModernKbm, Action::Fire, MotionInput(0, 1), kNewSlot) == BindStatus::Invalid,
          "mouse motion only goes to look and turn");
    Check(store.Bind(kModernKbm, Action::TurnLeft, MotionInput(0, -1), kNewSlot) == BindStatus::AlreadyBound,
          "turn takes mouse motion (here already bound)");
    Check(store.Bind(kN64Pad, Action::N64A, AxisInput(PadAxis::LeftTrigger, -1), kNewSlot) == BindStatus::Invalid,
          "a trigger only goes one way");
    Check(store.Bind(kN64Pad, Action::N64A, PadInput(PadButton::South), kNewSlot) == BindStatus::AlreadyBound,
          "an input the action already has");
}

void TestReserved() {
    BindingStore store;
    Check(store.Bind(kN64Kbm, Action::N64A, KeyInput(kScancodeEscape), kNewSlot) == BindStatus::Reserved,
          "Esc can't be bound");
    Check(store.Bind(kModernKbm, Action::Fire, KeyInput(kScancodeF2), kNewSlot) == BindStatus::Reserved,
          "F2 can't be bound");
    Check(store.Check(kN64Pad, Action::N64A, KeyInput(kScancodeEscape), kNewSlot).status == BindCheck::Status::Reserved,
          "reserved wins over a wrong-device input");
    Check(!Describe(BindCheck::Status::Reserved).empty(), "reserved inputs have an explanation");

    // Pause and menu navigation keep a binding.
    Check(store.Unbind(kModernKbm, Action::Pause, 0) == UnbindStatus::RequiredAction, "pause can't be left unbound");
    Check(store.Unbind(kModernPad, Action::MenuSelect, 0) == UnbindStatus::RequiredAction,
          "menu select can't be left unbound");
    Check(store.Unbind(kModernKbm, Action::MenuUp, 0) == UnbindStatus::Removed, "a second binding can go");
    Check(store.Unbind(kModernKbm, Action::MenuUp, 0) == UnbindStatus::RequiredAction, "but not the last");
    Check(store.Unbind(kModernKbm, Action::Crouch, 0) == UnbindStatus::Removed, "other actions can be unbound");
    Check(store.Unbind(kModernKbm, Action::Crouch, 0) == UnbindStatus::NotBound, "an empty slot");
    Check(store.Get(kModernKbm).Of(Action::Crouch).empty(), "crouch is unbound");
}

void TestConflicts() {
    BindingStore store;
    // N64 keyboard: E is L and R is R. Binding R's slot to E conflicts with L.
    const PhysicalInput e = KeyInput(18);
    const PhysicalInput r = KeyInput(19);
    const BindCheck check = store.Check(kN64Kbm, Action::N64R, e, 0);
    Check(check.status == BindCheck::Status::Conflict && check.conflicts.size() == 1 &&
              check.conflicts[0].action == Action::N64L,
          "E is held by L");
    Check(check.canReplace && check.canSwap, "L and R can both be replaced or swapped");
    Check(store.Bind(kN64Kbm, Action::N64R, e, 0) == BindStatus::NeedsDecision, "no choice yet means ask");
    Check(Has(store.Get(kN64Kbm), Action::N64L, e) && Has(store.Get(kN64Kbm), Action::N64R, r), "and nothing changed");
    const uint32_t before = store.Generation();
    Check(store.Bind(kN64Kbm, Action::N64R, e, 0, &kCancel) == BindStatus::Cancelled, "cancel");
    Check(store.Generation() == before, "cancel changes nothing");

    {
        BindingStore swapped;
        Check(swapped.Bind(kN64Kbm, Action::N64R, e, 0, &kSwap) == BindStatus::Bound, "swap");
        Check(Has(swapped.Get(kN64Kbm), Action::N64R, e) && Has(swapped.Get(kN64Kbm), Action::N64L, r) &&
                  !Has(swapped.Get(kN64Kbm), Action::N64L, e),
              "L and R trade inputs");
    }
    {
        BindingStore replaced;
        Check(replaced.Bind(kN64Kbm, Action::N64R, e, 0, &kReplace) == BindStatus::Bound, "replace");
        Check(Has(replaced.Get(kN64Kbm), Action::N64R, e) && replaced.Get(kN64Kbm).Of(Action::N64L).empty() &&
                  replaced.Get(kN64Kbm).Of(Action::N64R).size() == 1,
              "L loses E and R takes it, dropping its old key");
    }
    {
        // Adding to a new slot has nothing to swap.
        BindingStore added;
        const BindCheck fresh = added.Check(kN64Kbm, Action::N64R, e, kNewSlot);
        Check(fresh.status == BindCheck::Status::Conflict && fresh.canReplace && !fresh.canSwap,
              "a new slot has no input to give back");
        Check(added.Bind(kN64Kbm, Action::N64R, e, kNewSlot, &kSwap) == BindStatus::Refused, "so swap is refused");
    }
    {
        // Replace can't strip a required action.
        BindingStore required;
        const BindCheck strip = required.Check(kN64Kbm, Action::N64B, KeyInput(57), kNewSlot);
        Check(strip.status == BindCheck::Status::Conflict && !strip.canReplace, "Start has one binding and needs it");
        Check(required.Bind(kN64Kbm, Action::N64B, KeyInput(57), kNewSlot, &kReplace) == BindStatus::Refused,
              "replacing Start is refused");
        Check(required.Bind(kN64Kbm, Action::N64B, KeyInput(57), 0, &kSwap) == BindStatus::Bound,
              "but swapping it is fine");
        Check(required.Get(kN64Kbm).Of(Action::N64Start)[0] == KeyInput(46), "Start gets B's old key");
    }
}

void TestContexts() {
    BindingStore store;
    // The wheel is weapon change on foot and zoom while aiming.
    const Profile& kbm = store.Get(kModernKbm);
    Check(Has(kbm, Action::WeaponPrev, WheelInput(WheelDir::Up)) && Has(kbm, Action::ZoomIn, WheelInput(WheelDir::Up)),
          "the wheel serves two contexts");
    // Fire is live on foot and aiming, so it conflicts with both.
    BindCheck check = store.Check(kModernKbm, Action::Fire, WheelInput(WheelDir::Up), kNewSlot);
    Check(check.status == BindCheck::Status::Conflict && check.conflicts.size() == 2,
          "an action live in both contexts conflicts with both holders");
    // Crouch works on foot and aiming, so it conflicts with both weapon change and zoom.
    check = store.Check(kModernKbm, Action::Crouch, WheelInput(WheelDir::Down), kNewSlot);
    Check(check.status == BindCheck::Status::Conflict && check.conflicts.size() == 2,
          "crouch conflicts with weapon change and zoom");
    // Lean stays aiming only, so it shares nothing with weapon change.
    check = store.Check(kModernKbm, Action::LeanLeft, WheelInput(WheelDir::Down), kNewSlot);
    Check(check.status == BindCheck::Status::Conflict && check.conflicts.size() == 1 &&
              check.conflicts[0].action == Action::ZoomOut,
          "lean conflicts with zoom but not weapon change");
    // WASD is movement on foot and menu navigation: separate contexts.
    Check(Has(kbm, Action::MoveForward, KeyInput(17)) && Has(kbm, Action::MenuUp, KeyInput(17)),
          "W is move and menu up");
    // Menu-only vs gameplay: binding a menu action to a gameplay key is free.
    check = store.Check(kModernKbm, Action::MenuSelect, MouseInput(0), kNewSlot);
    Check(check.status == BindCheck::Status::Ok, "menu actions don't conflict with gameplay ones");
}

void TestResets() {
    BindingStore store;
    store.Bind(kN64Pad, Action::N64A, PadInput(PadButton::North), 0);
    store.Bind(kN64Pad, Action::N64CUp, PadInput(PadButton::West), 0);
    store.ResetSection(kN64Pad, Section::N64Face);
    Check(Has(store.Get(kN64Pad), Action::N64A, PadInput(PadButton::South)), "the section is back to defaults");
    Check(Has(store.Get(kN64Pad), Action::N64CUp, PadInput(PadButton::West)), "other sections keep their edits");
    store.ResetSection(kN64Pad, Section::ModernMove);
    Check(Has(store.Get(kN64Pad), Action::N64CUp, PadInput(PadButton::West)), "a section of another mode is ignored");
    store.ResetProfile(kN64Pad);
    Check(store.Get(kN64Pad) == DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad), "the profile is reset");

    store.Bind(kN64Pad, Action::N64A, PadInput(PadButton::North), 0);
    store.Bind(kModernKbm, Action::Detonate, KeyInput(34), kNewSlot);
    store.SetMode(2, BindingMode::Modern);
    store.ResetAll();
    Check(store.Get(kN64Pad) == DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad) &&
              store.Get(kModernKbm) == DefaultProfile(BindingMode::Modern, ProfileDevice::KeyboardMouse),
          "reset all resets every profile");
    Check(store.Mode(2) == kDefaultMode, "and every mode");
}

void TestSerialize() {
    BindingStore store;
    Check(store.Serialize(0) == "v1;mode=modern", "an untouched port stores nothing but its mode");
    store.Bind(kN64Pad, Action::N64A, PadInput(PadButton::North), 0);
    store.Bind(kModernKbm, Action::LookUp, KeyInput(34), 0);
    store.Bind(kModernKbm, Action::WeaponPrev, MouseInput(4), kNewSlot);
    store.Unbind(kModernKbm, Action::Crouch, 0);
    store.Bind(kModernPad, Action::ZoomIn, PadInput(PadButton::Back), 0);
    store.SetMode(0, BindingMode::Modern);

    BindingStore loaded;
    Check(loaded.Deserialize(0, store.Serialize(0)), "a stored port loads");
    Check(loaded.Mode(0) == BindingMode::Modern, "with its mode");
    for (const BindTarget& target : { kN64Pad, kN64Kbm, kModernKbm, kModernPad }) {
        Check(loaded.Get(target) == store.Get(target), "and every profile");
    }
    Check(loaded.Get(kModernKbm).Of(Action::Crouch).empty(), "an unbound action stays unbound");
    Check(loaded.Serialize(0) == store.Serialize(0), "and writes back the same text");

    // Untouched profiles are left to the defaults, so new defaults reach them.
    Check(store.Serialize(0).find("n64.kbm") == std::string::npos, "untouched profiles aren't stored");

    // Ports are independent.
    Check(loaded.Get({ 1, BindingMode::N64, ProfileDevice::Gamepad }) ==
              DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad),
          "other ports aren't affected");
}

void TestDeserializeLeniency() {
    BindingStore store;
    for (const char* junk : { "", "garbage", "v2;mode=n64", "mode=n64" }) {
        const uint32_t generation = store.Generation();
        Check(!store.Deserialize(0, junk) && store.Generation() == generation,
              "a string that isn't a store is refused");
    }

    // Unknown actions, bad tokens, reserved and wrong-device inputs are dropped;
    // a required action left bare gets its default back.
    const std::string text = "v1;mode=bogus;n64.kbm=n64.a:k45,k1,p3,zzz,k45|n64.l:|n64.start:|nope.x:k5;"
                             "n64.kbm.extra=1;future=1";
    Check(store.Deserialize(0, text), "a store with oddities loads");
    const Profile& profile = store.Get(kN64Kbm);
    Check(profile.Of(Action::N64A).size() == 1 && profile.Of(Action::N64A)[0] == KeyInput(45),
          "reserved, wrong-device, malformed and duplicate inputs are dropped");
    Check(profile.Of(Action::N64L).empty(), "a bare optional action stays bare");
    Check(profile.Of(Action::N64Start) ==
              DefaultProfile(BindingMode::N64, ProfileDevice::KeyboardMouse).Of(Action::N64Start),
          "a bare required action gets its default");
    Check(profile.Of(Action::N64Z) == DefaultProfile(BindingMode::N64, ProfileDevice::KeyboardMouse).Of(Action::N64Z),
          "an action the string leaves out reads as default");
    Check(store.Mode(0) == kDefaultMode, "an unknown mode reads as the default");
}

void TestMigration() {
    // Nothing legacy for a device type: the defaults.
    Check(ProfileFromLegacy({}, ProfileDevice::Gamepad) == DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad),
          "no legacy bindings gives the defaults");

    const std::vector<LegacyBinding> legacy = {
        { Action::N64A, KeyInput(30) },
        { Action::N64A, KeyInput(45) },
        { Action::N64B, KeyInput(kScancodeEscape) },
        { Action::N64Z, PadInput(PadButton::South) },
        { Action::N64L, MouseInput(0) },
    };
    const Profile kbm = ProfileFromLegacy(legacy, ProfileDevice::KeyboardMouse);
    Check(kbm.Of(Action::N64A).size() == 2 && kbm.Of(Action::N64L).size() == 1,
          "legacy keyboard and mouse bindings carry over");
    Check(kbm.Of(Action::N64Z).empty() && kbm.Of(Action::N64B).size() == 1 && kbm.Of(Action::N64B)[0] == KeyInput(46),
          "reserved and wrong-device legacy inputs are dropped; a required action left bare gets its default");
    Check(kbm.Of(Action::N64R).empty(), "an optional action the player had unbound stays unbound");

    const Profile pad = ProfileFromLegacy(legacy, ProfileDevice::Gamepad);
    Check(pad.Of(Action::N64Z).size() == 1 &&
              pad.Of(Action::N64A) == DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad).Of(Action::N64A),
          "the gamepad profile takes only gamepad bindings");

    BindingStore store;
    store.SetProfile(kN64Kbm, kbm);
    Check(store.Get(kN64Kbm) == kbm, "a migrated profile is stored");
}

PortDevice Kbm() {
    return { DeviceKind::KeyboardMouse, {}, 0 };
}
PortDevice PadA() {
    return { DeviceKind::Gamepad, "Pad A", 0 };
}
const AttachedDevices kAttached{ { { 4, "Pad A" }, { 7, "Pad B" } }, true };

RawInput FromKeyboard(int scancode) {
    return { { InputSource::Kind::Keyboard, 0 }, KeyInput(scancode) };
}
RawInput FromPad(int id, PadButton button) {
    return { { InputSource::Kind::Gamepad, id }, PadInput(button) };
}

void TestCapture() {
    // Only the selected device binds.
    {
        Capture capture;
        Check(capture.Begin(kN64Pad, Action::N64A, 0, PadA(), kAttached, 1000), "capture starts on an attached pad");
        Check(capture.Update(1000, {}) == Capture::State::Waiting, "nothing pressed yet");
        Check(capture.Update(1100, { FromKeyboard(17) }) == Capture::State::Waiting, "the keyboard is ignored");
        Check(capture.Hint().find("Pad A") != std::string::npos, "with a hint naming the device it reads");
        Check(capture.Update(1200, { FromPad(7, PadButton::North) }) == Capture::State::Waiting,
              "another pad is ignored");
        Check(capture.Hint().find("Pad B") != std::string::npos, "and named");
        Check(capture.Update(1300, { FromPad(4, PadButton::North) }) == Capture::State::Captured,
              "the selected pad binds");
        Check(capture.Result() == PadInput(PadButton::North), "with its input");
        Check(capture.Target() == kN64Pad && capture.TargetAction() == Action::N64A && capture.Slot() == 0,
              "for the target it was started with");
    }
    {
        Capture capture;
        capture.Begin(kN64Kbm, Action::N64A, kNewSlot, Kbm(), kAttached, 0);
        Check(capture.Update(10, { FromPad(4, PadButton::South) }) == Capture::State::Waiting,
              "a gamepad is ignored while the keyboard is selected");
        Check(capture.Update(20, { { { InputSource::Kind::Mouse, 0 }, MouseInput(0) } }) == Capture::State::Captured,
              "the mouse counts as the keyboard device");
    }
    // Esc cancels, from any device; reserved F2 is refused.
    {
        Capture capture;
        capture.Begin(kN64Pad, Action::N64A, 0, PadA(), kAttached, 0);
        capture.Update(0, {});
        Check(capture.Update(10, { FromKeyboard(kScancodeEscape) }) == Capture::State::Cancelled,
              "Esc cancels even with a pad selected");
    }
    {
        Capture capture;
        capture.Begin(kN64Kbm, Action::N64A, 0, Kbm(), kAttached, 0);
        capture.Update(0, {});
        Check(capture.Update(10, { FromKeyboard(kScancodeF2) }) == Capture::State::Waiting, "F2 isn't captured");
        Check(!capture.Hint().empty(), "and says why");
        Check(capture.Update(20, { FromKeyboard(17) }) == Capture::State::Captured,
              "capture carries on to the next key");
    }
    // Cancel button.
    {
        Capture capture;
        capture.Begin(kN64Kbm, Action::N64A, 0, Kbm(), kAttached, 0);
        capture.Cancel();
        Check(capture.state() == Capture::State::Cancelled && !capture.Waiting(), "the cancel button");
        Check(capture.Update(5, { FromKeyboard(17) }) == Capture::State::Cancelled,
              "a cancelled capture stays cancelled");
        capture.Reset();
        Check(capture.state() == Capture::State::Idle, "reset returns to idle");
    }
    // Timeout.
    {
        Capture capture;
        capture.Begin(kN64Kbm, Action::N64A, 0, Kbm(), kAttached, 1000, 500);
        Check(capture.RemainingMs(1200) == 300, "time left");
        Check(capture.Update(1499, {}) == Capture::State::Waiting, "still waiting");
        Check(capture.Update(1500, {}) == Capture::State::TimedOut, "times out");
        Check(capture.RemainingMs(1600) == 0, "no time left after");
    }
    // The press that started the capture doesn't bind until released.
    {
        Capture capture;
        capture.Begin(kN64Kbm, Action::N64A, 0, Kbm(), kAttached, 0);
        const RawInput click{ { InputSource::Kind::Mouse, 0 }, MouseInput(0) };
        Check(capture.Update(0, { click }) == Capture::State::Waiting, "a button already down is ignored");
        Check(capture.Update(10, { click }) == Capture::State::Waiting, "while it stays down");
        Check(capture.Update(20, {}) == Capture::State::Waiting, "released");
        Check(capture.Update(30, { click }) == Capture::State::Captured, "pressed again binds");
    }
    // Inputs the action can't take are skipped.
    {
        Capture capture;
        capture.Begin(kModernKbm, Action::Fire, 0, Kbm(), kAttached, 0);
        capture.Update(0, {});
        const RawInput motion{ { InputSource::Kind::Mouse, 0 }, MotionInput(0, 1) };
        Check(capture.Update(5, { motion }) == Capture::State::Waiting, "mouse motion can't bind fire");
        capture.Begin(kModernKbm, Action::TurnLeft, 0, Kbm(), kAttached, 0);
        capture.Update(0, {});
        Check(capture.Update(5, { motion }) == Capture::State::Captured, "but it can bind turn");
    }
    // Refusals.
    {
        Capture capture;
        Check(!capture.Begin(kN64Pad, Action::N64A, 0, PortDevice{}, kAttached, 0) && !capture.Hint().empty(),
              "default bindings have no device of their own to capture from");
        Check(!capture.Begin(kN64Pad, Action::N64A, 0, PortDevice{ DeviceKind::None, {}, 0 }, kAttached, 0),
              "nor does None");
        Check(!capture.Begin(kN64Pad, Action::N64A, 0, PortDevice{ DeviceKind::Gamepad, "Gone", 0 }, kAttached, 0) &&
                  capture.Hint().find("Gone") != std::string::npos,
              "a disconnected pad says so");
        Check(!capture.Begin(kN64Kbm, Action::N64A, 0, PadA(), kAttached, 0), "the profile must match the device");
        Check(!capture.Begin(kN64Pad, Action::MoveForward, 0, PadA(), kAttached, 0), "and the action the mode");
        Check(capture.state() == Capture::State::Idle, "a refused capture stays idle");
    }
}

} // namespace

int main() {
    TestDefaults();
    TestLabels();
    TestProfilesStaySeparate();
    TestValidity();
    TestReserved();
    TestConflicts();
    TestContexts();
    TestResets();
    TestSerialize();
    TestDeserializeLeniency();
    TestMigration();
    TestCapture();
    return gFailures == 0 ? 0 : 1;
}
