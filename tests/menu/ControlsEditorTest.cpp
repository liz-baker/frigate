#include <cstdio>
#include <string>
#include <vector>

#include "menu/BindingFlow.h"
#include "menu/ControlsModel.h"
#include "menu/System.h"

using namespace Frigate::ConfigMenu;
using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

PortDevice Kbm() {
    PortDevice device;
    device.kind = DeviceKind::KeyboardMouse;
    return device;
}

PortDevice Pad(const char* name = "Pad") {
    PortDevice device;
    device.kind = DeviceKind::Gamepad;
    device.name = name;
    return device;
}

AttachedDevices WithKbmAndPad() {
    AttachedDevices attached;
    attached.keyboardMouse = true;
    attached.gamepads = { { 7, "Pad" } };
    return attached;
}

RawInput KeyDown(int scancode) {
    RawInput raw;
    raw.source.kind = InputSource::Kind::Keyboard;
    raw.input = KeyInput(scancode);
    return raw;
}

RawInput PadDown(int32_t id, PadButton button) {
    RawInput raw;
    raw.source.kind = InputSource::Kind::Gamepad;
    raw.source.gamepadId = id;
    raw.input = PadInput(button);
    return raw;
}

PortInputs InputsFor(const PortDevice& device, const AttachedDevices& attached) {
    PortInputs inputs;
    inputs.port = 0;
    inputs.device = device;
    inputs.attached = attached;
    return inputs;
}

const SectionView* FindSection(const PortView& view, Section section) {
    for (const SectionView& candidate : view.sections) {
        if (candidate.section == section) {
            return &candidate;
        }
    }
    return nullptr;
}

const RowView* FindRow(const PortView& view, Action action) {
    for (const SectionView& section : view.sections) {
        for (const RowView& row : section.rows) {
            if (row.action == action) {
                return &row;
            }
        }
    }
    return nullptr;
}

void TestDisabledReasons() {
    const AttachedDevices attached = WithKbmAndPad();
    BindingStore store;

    PortView view = BuildPortView(InputsFor({}, attached), store);
    Check(!view.editable && !view.disabledReason.empty(), "default bindings port is not editable, with a reason");
    Check(view.sections.empty(), "no sections without a device");

    PortDevice none;
    none.kind = DeviceKind::None;
    view = BuildPortView(InputsFor(none, attached), store);
    Check(!view.editable && view.sections.empty(), "None is not editable");

    view = BuildPortView(InputsFor(Pad("Gone"), attached), store);
    Check(!view.editable && view.disabledReason.find("Gone") != std::string::npos,
          "a disconnected pad names itself in the reason");
    Check(!view.sections.empty(), "a disconnected device's bindings stay listed, disabled");

    view = BuildPortView(InputsFor(Kbm(), attached), store);
    Check(view.editable && view.disabledReason.empty(), "an attached keyboard is editable");
    view = BuildPortView(InputsFor(Pad(), attached), store);
    Check(view.editable, "an attached pad is editable");
    AttachedDevices noKbm = attached;
    noKbm.keyboardMouse = false;
    view = BuildPortView(InputsFor(Kbm(), noKbm), store);
    Check(!view.editable, "a keyboard that isn't attached is not editable");
}

void TestSections() {
    BindingStore store;
    const AttachedDevices attached = WithKbmAndPad();

    PortView view = BuildPortView(InputsFor(Kbm(), attached), store);
    Check(view.target.mode == kDefaultMode && kDefaultMode == BindingMode::Modern, "the port starts in Modern mode");
    store.SetMode(0, BindingMode::N64);
    view = BuildPortView(InputsFor(Kbm(), attached), store);
    Check(view.sections.size() == SectionsOf(BindingMode::N64).size(), "N64 mode lists the N64 sections");
    Check(FindSection(view, Section::N64Stick) != nullptr, "the N64 stick is one section");
    int sticks = 0;
    for (const SectionView& section : view.sections) {
        sticks += section.section == Section::N64Stick;
    }
    Check(sticks == 1, "exactly one stick section");
    Check(FindSection(view, Section::N64Stick)->rows.size() == 4, "the stick section maps four directions");
    Check(!view.showMouseSensitivity && !view.showLookSensitivity, "N64 mode has no look sensitivity");
    for (const SectionView& section : view.sections) {
        Check(section.rows.size() == ActionsOf(section.section).size(), "a section lists its actions");
    }

    store.SetMode(0, BindingMode::Modern);
    view = BuildPortView(InputsFor(Kbm(), attached), store);
    Check(view.target.mode == BindingMode::Modern, "the view follows the mode");
    Check(view.sections.size() == SectionsOf(BindingMode::Modern).size(), "Modern mode lists the Modern sections");
    Check(FindRow(view, Action::Fire) != nullptr && FindRow(view, Action::N64A) == nullptr,
          "only the selected mode's actions are listed");
    Check(view.showMouseSensitivity && !view.showLookSensitivity, "a keyboard offers the mouse sensitivity");
    Check(view.showCrouchToggle && !view.crouchToggle, "Modern mode offers the crouch setting, hold by default");
    PortInputs toggling = InputsFor(Kbm(), attached);
    toggling.crouchToggle = true;
    view = BuildPortView(toggling, store);
    Check(view.showCrouchToggle && view.crouchToggle, "and shows it as toggle when set");
    toggling.device = Pad();
    toggling.lookSensitivity = { 150, 100, 10, 300 };
    view = BuildPortView(toggling, store);
    Check(view.showCrouchToggle, "for a pad as well");
    Check(view.showLookSensitivity && !view.showMouseSensitivity && view.lookSensitivity.value == 150,
          "a pad offers the right stick's look sensitivity instead");
    BindingStore n64;
    n64.SetMode(0, BindingMode::N64);
    view = BuildPortView(toggling, n64);
    Check(!view.showCrouchToggle && !view.crouchToggle, "N64 mode has no crouch setting");

    store.SetMode(0, BindingMode::N64);
    view = BuildPortView(InputsFor(Pad(), attached), store);
    Check(view.profileDevice == ProfileDevice::Gamepad && view.target.device == ProfileDevice::Gamepad,
          "a pad edits the gamepad profile");
    const RowView* a = FindRow(view, Action::N64A);
    Check(a != nullptr && !a->slots.empty() &&
              a->slots[0].label == InputLabel(store.Get(view.target).Of(Action::N64A)[0], FaceLayout::Xbox),
          "slot labels come from the bound inputs");
}

void TestDeviceSettings() {
    BindingStore store;
    store.SetMode(0, BindingMode::N64);
    const AttachedDevices attached = WithKbmAndPad();
    ControllerFeatures all;
    all.rumble = all.rumbleAdd = all.led = all.gyro = true;

    PortInputs inputs = InputsFor(Kbm(), attached);
    inputs.features = all;
    PortView view = BuildPortView(inputs, store);
    Check(!view.ShowDeviceSettings() && !view.isController, "a keyboard and mouse port has no device settings");
    Check(view.faceLegend.empty(), "a keyboard has no face button legend");

    inputs = InputsFor(Pad(), attached);
    inputs.features = all;
    view = BuildPortView(inputs, store);
    Check(view.isController && view.showStick && view.showRumble && view.showLed && view.showGyro,
          "a controller shows every setting the platform supports");

    inputs.features = {};
    view = BuildPortView(inputs, store);
    Check(view.showStick && !view.showRumble && !view.showLed && !view.showGyro,
          "a platform without rumble, LED and gyro hides them and keeps the stick options");

    inputs.features.rumble = true;
    view = BuildPortView(inputs, store);
    Check(view.showRumble && !view.showLed && !view.showGyro, "each setting follows its own feature");

    PortDevice none;
    none.kind = DeviceKind::None;
    view = BuildPortView(InputsFor(none, attached), store);
    Check(!view.ShowDeviceSettings(), "no device, no settings");
}

void TestFaceLabels() {
    Check(FaceLegend(FaceLayout::Xbox) == "South A, East B, West X, North Y", "Xbox face legend");
    Check(FaceLegend(FaceLayout::Nintendo) == "South B, East A, West Y, North X", "Nintendo face legend");
    Check(FaceLegend(FaceLayout::PlayStation) == "South Cross, East Circle, West Square, North Triangle",
          "PlayStation face legend");

    BindingStore store;
    store.SetMode(0, BindingMode::N64);
    PortInputs inputs = InputsFor(Pad(), WithKbmAndPad());
    inputs.layout = FaceLayout::Nintendo;
    PortView view = BuildPortView(inputs, store);
    Check(!view.faceLegend.empty() && view.faceLegend.find("South B") != std::string::npos,
          "the legend follows the device's layout");
    const RowView* a = FindRow(view, Action::N64A);
    const PhysicalInput bound = store.Get(view.target).Of(Action::N64A)[0];
    Check(a != nullptr && a->slots[0].label == InputLabel(bound, FaceLayout::Nintendo),
          "slot labels use the device's layout");
    inputs.layout = FaceLayout::Xbox;
    view = BuildPortView(inputs, store);
    Check(FindRow(view, Action::N64A)->slots[0].label == InputLabel(bound, FaceLayout::Xbox),
          "changing the layout relabels");
}

void TestHeldHighlight() {
    BindingStore store;
    store.SetMode(0, BindingMode::N64);
    const AttachedDevices attached = WithKbmAndPad();
    const PhysicalInput aKey = store.Get({ 0, BindingMode::N64, ProfileDevice::KeyboardMouse }).Of(Action::N64A)[0];

    PortInputs inputs = InputsFor(Kbm(), attached);
    inputs.held = { KeyDown(aKey.code) };
    PortView view = BuildPortView(inputs, store);
    Check(FindRow(view, Action::N64A)->active && FindRow(view, Action::N64A)->slots[0].held,
          "a held bound key lights its action");
    Check(!FindRow(view, Action::N64B)->active, "other actions stay dark");

    inputs.held = { PadDown(7, PadButton::South) };
    view = BuildPortView(inputs, store);
    Check(!FindRow(view, Action::N64A)->active, "pad input doesn't light a keyboard port");

    const PhysicalInput south = store.Get({ 0, BindingMode::N64, ProfileDevice::Gamepad }).Of(Action::N64A)[0];
    inputs = InputsFor(Pad(), attached);
    inputs.held = { PadDown(7, static_cast<PadButton>(south.code)) };
    view = BuildPortView(inputs, store);
    Check(FindRow(view, Action::N64A)->active, "a held pad button lights its action");

    inputs.held = { PadDown(9, static_cast<PadButton>(south.code)), KeyDown(aKey.code) };
    view = BuildPortView(inputs, store);
    Check(!FindRow(view, Action::N64A)->active, "another gamepad or the keyboard doesn't light a pad port");
}

void TestRemovable() {
    BindingStore store;
    store.SetMode(0, BindingMode::N64);
    const AttachedDevices attached = WithKbmAndPad();
    PortView view = BuildPortView(InputsFor(Kbm(), attached), store);
    const RowView* start = FindRow(view, Action::N64Start);
    Check(start != nullptr && start->required && !start->slots.empty() && !start->slots[0].canRemove,
          "the last binding of a required action can't be removed");
    const RowView* l = FindRow(view, Action::N64L);
    Check(l != nullptr && !l->required && l->slots[0].canRemove, "an optional action's binding can be removed");

    const BindTarget target = view.target;
    Check(store.Bind(target, Action::N64Start, KeyInput(100), kNewSlot) == BindStatus::Bound, "add a second binding");
    view = BuildPortView(InputsFor(Kbm(), attached), store);
    start = FindRow(view, Action::N64Start);
    Check(start->slots.size() == 2 && start->slots[0].canRemove, "with two bindings either can go");

    for (int key = 101; key < 100 + static_cast<int>(kMaxSlots); key++) {
        store.Bind(target, Action::N64Start, KeyInput(key), kNewSlot);
    }
    view = BuildPortView(InputsFor(Kbm(), attached), store);
    Check(!FindRow(view, Action::N64Start)->canAdd, "the slot limit stops adding");
}

void TestLabels() {
    Check(CountdownSeconds(0) == 0 && CountdownSeconds(1) == 1 && CountdownSeconds(1000) == 1 &&
              CountdownSeconds(1001) == 2 && CountdownSeconds(-5) == 0,
          "countdown rounds up");
    Check(BindAllProgress(3, 18, "Start").find("3 of 18") != std::string::npos, "Bind all progress");
    Check(CapturePrompt(Action::N64A, kNewSlot).find("adds a binding") != std::string::npos, "new slot prompt");
    Check(CapturePrompt(Action::N64A, 0).find("adds") == std::string::npos, "rebind prompt");
    Check(std::string(ModeName(BindingMode::Modern)) == "Modern" && std::string(ModeName(BindingMode::N64)) == "N64",
          "mode names");
}

// A flow on port 0's keyboard profile.
struct Harness {
    BindingStore store;
    BindingFlow flow;
    BindTarget target{ 0, BindingMode::N64, ProfileDevice::KeyboardMouse };
    AttachedDevices attached = WithKbmAndPad();
    int64_t now = 1000;

    bool Single(Action action, size_t slot) {
        return flow.BeginSingle(target, action, slot, Kbm(), attached, FaceLayout::Xbox, now, store);
    }
    // Updates with nothing held (the baseline), then with `held`.
    void Press(int scancode) {
        flow.Update(store, now += 16, {});
        flow.Update(store, now += 16, { KeyDown(scancode) });
        flow.Update(store, now += 16, {});
    }
};

void TestSingleCapture() {
    Harness h;
    Check(!h.flow.BeginSingle(h.target, Action::N64A, 0, {}, h.attached, FaceLayout::Xbox, h.now, h.store),
          "capture refused on Default bindings");
    Check(!h.flow.Message().empty() && !h.flow.Active(), "the refusal says why");

    Check(h.Single(Action::N64A, 0), "capture begins");
    Check(h.flow.Active() && h.flow.phase() == BindingFlow::Phase::Capturing, "capturing");
    Check(h.flow.RemainingMs(h.now) > 0, "countdown running");
    h.Press(100);
    Check(!h.flow.Active(), "a single capture ends once bound");
    Check(h.flow.TakeChanged() && !h.flow.TakeChanged(), "the change is reported once");
    Check(h.store.Get(h.target).Of(Action::N64A)[0] == KeyInput(100), "the key replaced slot 0");

    Check(h.Single(Action::N64A, kNewSlot), "add begins");
    h.Press(101);
    Check(h.store.Get(h.target).Of(Action::N64A).size() == 2 &&
              h.store.Get(h.target).Of(Action::N64A)[1] == KeyInput(101),
          "a new slot adds a binding");

    // Esc cancels and changes nothing.
    Check(h.Single(Action::N64B, 0), "begin again");
    const PhysicalInput before = h.store.Get(h.target).Of(Action::N64B)[0];
    h.flow.TakeChanged();
    h.Press(kScancodeEscape);
    Check(!h.flow.Active() && h.store.Get(h.target).Of(Action::N64B)[0] == before, "Esc cancels");
    Check(!h.flow.TakeChanged(), "a cancel changes nothing");

    // The Cancel button.
    Check(h.Single(Action::N64B, 0), "begin for the button");
    h.flow.Cancel();
    Check(!h.flow.Active() && h.flow.Message() == "Cancelled.", "Cancel ends the flow");

    // Timeout.
    Check(h.Single(Action::N64B, 0), "begin for the timeout");
    for (int64_t elapsed = 0; elapsed <= kCaptureTimeoutMs; elapsed += 500) {
        h.flow.Update(h.store, h.now += 500, {});
    }
    Check(!h.flow.Active() && h.flow.Message().find("Timed out") != std::string::npos, "the capture times out");

    // Closing the menu for a while drops the flow.
    Check(h.Single(Action::N64B, 0), "begin for staleness");
    h.flow.Update(h.store, h.now += 16, {});
    h.flow.Update(h.store, h.now += kFlowStaleMs + 1, { KeyDown(102) });
    Check(!h.flow.Active() && h.store.Get(h.target).Of(Action::N64B)[0] == before,
          "a long gap cancels instead of binding what is pressed on return");

    // Input from another device is ignored with a hint.
    Check(h.Single(Action::N64B, 0), "begin for another device");
    h.flow.Update(h.store, h.now += 16, {});
    h.flow.Update(h.store, h.now += 16, { PadDown(7, PadButton::South) });
    Check(h.flow.phase() == BindingFlow::Phase::Capturing && !h.flow.Hint().empty(),
          "a pad is ignored on a keyboard port");
}

void TestConflicts() {
    Harness h;
    const PhysicalInput rKey0 = h.store.Get(h.target).Of(Action::N64R)[0];
    const PhysicalInput lKey0 = h.store.Get(h.target).Of(Action::N64L)[0];

    // Swap: L takes R's key, R takes L's old key.
    Check(h.Single(Action::N64L, 0), "begin on L");
    h.Press(rKey0.code);
    Check(h.flow.phase() == BindingFlow::Phase::Deciding, "a used input asks");
    Check(h.flow.Prompt().canSwap && h.flow.Prompt().canReplace, "swap and replace are offered");
    Check(h.flow.Prompt().text.find(Info(Action::N64R).label) != std::string::npos,
          "the prompt names the other action");
    h.flow.Decide(h.store, ConflictChoice::Swap);
    Check(!h.flow.Active() && h.flow.TakeChanged(), "swap ends the flow with a change");
    Check(h.store.Get(h.target).Of(Action::N64L)[0] == rKey0 && h.store.Get(h.target).Of(Action::N64R)[0] == lKey0,
          "swap exchanged the inputs");

    // Replace: the other action loses the input.
    Check(h.Single(Action::N64L, 0), "begin on L again");
    const PhysicalInput zKey = h.store.Get(h.target).Of(Action::N64Z)[0];
    h.Press(zKey.code);
    Check(h.flow.phase() == BindingFlow::Phase::Deciding, "asks again");
    h.flow.Decide(h.store, ConflictChoice::Replace);
    h.flow.TakeChanged();
    Check(h.store.Get(h.target).Of(Action::N64L)[0] == zKey && h.store.Get(h.target).Of(Action::N64Z).empty(),
          "replace took the input from Z");

    // Cancel leaves everything.
    Check(h.Single(Action::N64R, 0), "begin on R");
    const PhysicalInput r = h.store.Get(h.target).Of(Action::N64R)[0];
    h.Press(h.store.Get(h.target).Of(Action::N64DUp)[0].code);
    Check(h.flow.phase() == BindingFlow::Phase::Deciding, "asks a third time");
    h.flow.Decide(h.store, ConflictChoice::Cancel);
    Check(!h.flow.Active() && h.store.Get(h.target).Of(Action::N64R)[0] == r, "cancel changes nothing");
    Check(!h.flow.TakeChanged(), "no change reported");

    // Replace is unavailable when it would leave a required action bare.
    Check(h.Single(Action::N64L, 0), "begin on L for the required case");
    h.Press(h.store.Get(h.target).Of(Action::N64Start)[0].code);
    Check(h.flow.phase() == BindingFlow::Phase::Deciding, "asks for a required action's input");
    Check(!h.flow.Prompt().canReplace && !h.flow.Prompt().replaceBlocked.empty(), "replace is blocked with the reason");
    h.flow.Decide(h.store, ConflictChoice::Replace);
    Check(h.flow.TakeChanged() == false, "a blocked replace changes nothing");
}

void TestBindAll() {
    Harness h;
    const std::vector<Action> actions = ActionsOf(BindingMode::N64);
    Check(h.flow.BeginBindAll(h.target, actions, Kbm(), h.attached, FaceLayout::Xbox, h.now, h.store),
          "Bind all begins");
    Check(h.flow.BindAll() && h.flow.Steps() == actions.size() && h.flow.Step() == 1, "starts at the first action");
    Check(h.flow.CurrentAction() == actions[0], "prompts the first action");

    // Bind the first two, skip the third, bind the fourth.
    h.Press(100);
    Check(h.flow.Step() == 2 && h.flow.CurrentAction() == actions[1], "advances after a binding");
    h.Press(101);
    Check(h.flow.Step() == 3, "advances again");
    const PhysicalInput third = h.store.Get(h.target).Of(actions[2])[0];
    h.flow.Skip(h.store);
    Check(h.flow.Step() == 4 && h.store.Get(h.target).Of(actions[2])[0] == third, "Skip leaves the action alone");
    h.Press(103);
    Check(h.store.Get(h.target).Of(actions[0])[0] == KeyInput(100) &&
              h.store.Get(h.target).Of(actions[1])[0] == KeyInput(101) &&
              h.store.Get(h.target).Of(actions[3])[0] == KeyInput(103),
          "each action got its key in order");
    Check(h.flow.TakeChanged(), "the store changed");

    // A conflict mid-sequence asks, and the answer continues the walk.
    h.Press(h.store.Get(h.target).Of(actions[0])[0].code);
    Check(h.flow.phase() == BindingFlow::Phase::Deciding && h.flow.Step() == 5, "a conflict asks at the same step");
    h.flow.Decide(h.store, ConflictChoice::Cancel);
    Check(h.flow.Active() && h.flow.phase() == BindingFlow::Phase::Capturing && h.flow.Step() == 5,
          "cancelling the conflict re-prompts the same action");

    // Cancel all ends the sequence.
    h.flow.Cancel();
    Check(!h.flow.Active(), "Cancel all ends");

    // Esc during a step ends the whole sequence.
    Check(h.flow.BeginBindAll(h.target, actions, Kbm(), h.attached, FaceLayout::Xbox, h.now, h.store), "begin again");
    h.Press(kScancodeEscape);
    Check(!h.flow.Active(), "Esc cancels Bind all");

    // Binding every action finishes the walk.
    Harness full;
    Check(full.flow.BeginBindAll(full.target, actions, Kbm(), full.attached, FaceLayout::Xbox, full.now, full.store),
          "begin the full walk");
    for (size_t i = 0; i < actions.size(); i++) {
        Check(full.flow.Active() && full.flow.CurrentAction() == actions[i], "walks the actions in order");
        full.Press(100 + static_cast<int>(i));
    }
    Check(!full.flow.Active() && full.flow.Message() == "Bind all finished.", "the last action ends the sequence");
    Check(full.store.Get(full.target).Of(actions.back())[0] == KeyInput(100 + static_cast<int>(actions.size()) - 1),
          "the last key was bound");

    // Only the first binding of an action is replaced.
    Harness extra;
    extra.store.Bind(extra.target, actions[0], KeyInput(150), kNewSlot);
    Check(extra.flow.BeginBindAll(extra.target, { actions[0] }, Kbm(), extra.attached, FaceLayout::Xbox, extra.now,
                                  extra.store),
          "begin on an action with two bindings");
    extra.Press(100);
    Check(extra.store.Get(extra.target).Of(actions[0]).size() == 2 &&
              extra.store.Get(extra.target).Of(actions[0])[0] == KeyInput(100) &&
              extra.store.Get(extra.target).Of(actions[0])[1] == KeyInput(150),
          "Bind all replaces slot 0 and keeps the others");

    // Nothing to walk, or no device.
    Check(!extra.flow.BeginBindAll(extra.target, {}, Kbm(), extra.attached, FaceLayout::Xbox, extra.now, extra.store),
          "an empty list does nothing");
    Check(!extra.flow.BeginBindAll(extra.target, actions, {}, extra.attached, FaceLayout::Xbox, extra.now, extra.store),
          "no device, no Bind all");
}

void TestPromptText() {
    BindCheck check;
    check.status = BindCheck::Status::Conflict;
    check.conflicts = { { Action::N64A, 0 }, { Action::N64B, 0 }, { Action::N64Z, 0 } };
    const ConflictPrompt prompt = BuildConflictPrompt(check, KeyInput(100), FaceLayout::Xbox);
    Check(prompt.text.find("A, B and Z") != std::string::npos, "several conflicts read as a list");
    Check(!prompt.canSwap && !prompt.canReplace && !prompt.replaceBlocked.empty(), "options copy the check");
}

class DefaultSystem : public System {};

void TestSystemDefaults() {
    DefaultSystem system;
    Check(!system.PortConnected(2), "a port starts disconnected");
    system.SetPortConnected(2, true);
    Check(system.PortConnected(2) && !system.PortConnected(1), "connected is per port");
    system.SetPortConnected(2, false);
    Check(!system.PortConnected(2), "and can be cleared");
    Check(!system.CrouchToggle(1), "crouch is held by default");
    system.SetCrouchToggle(1, true);
    Check(system.CrouchToggle(1) && !system.CrouchToggle(0) && !system.CrouchToggle(2), "toggle is per port");
    system.SetCrouchToggle(1, false);
    Check(!system.CrouchToggle(1), "and can be cleared");
    Check(system.MouseSensitivity(0).value == 100 && system.MouseSensitivity(0).IsDefault(),
          "the mouse sensitivity starts at 100%");
    system.SetMouseSensitivity(0, 5000);
    Check(system.MouseSensitivity(0).value == system.MouseSensitivity(0).max && system.MouseSensitivity(1).IsDefault(),
          "and is clamped per port");
    system.SetLookSensitivity(2, 40);
    Check(system.LookSensitivity(2).value == 40 && system.LookSensitivity(0).IsDefault(),
          "the look sensitivity is its own setting");
    const ControllerFeatures features = system.Features(0);
    Check(!features.rumble && !features.led && !features.gyro && !features.rumbleAdd,
          "a system with no controllers offers no device settings");
    Check(system.RumbleDevices(0).empty() && system.LedDevices(0).empty() && !system.Gyro(0).present,
          "and lists no devices");
}

} // namespace

int main() {
    TestDisabledReasons();
    TestSections();
    TestDeviceSettings();
    TestFaceLabels();
    TestHeldHighlight();
    TestRemovable();
    TestLabels();
    TestSingleCapture();
    TestConflicts();
    TestBindAll();
    TestPromptText();
    TestSystemDefaults();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
