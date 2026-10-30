#include "platform/PortAssignment.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

using Ids = std::vector<int32_t>;

const PortDevice kPadA{ DeviceKind::Gamepad, "Pad A", 0 };
const PortDevice kPadB{ DeviceKind::Gamepad, "Pad B", 0 };
const PortDevice kKbm{ DeviceKind::KeyboardMouse, {}, 0 };
const PortDevice kNone{ DeviceKind::None, {}, 0 };
const AttachedDevices kBoth{ { { 4, "Pad A" }, { 7, "Pad B" } }, true };

void TestConnectedPorts() {
    // A forced port joins the detected ones; ports 3 and 4 count like port 2.
    Check(EffectiveConnectedPorts(0x0, 0x0) == 0x0, "nothing detected or forced");
    Check(EffectiveConnectedPorts(0x1, 0x2) == 0x3, "a forced port 2 joins port 1");
    Check(EffectiveConnectedPorts(0x0, 0xF) == 0xF, "all four forced with nothing attached");
    Check(EffectiveConnectedPorts(0x3, 0x3) == 0x3, "forcing an attached port changes nothing");
    Check(EffectiveConnectedPorts(0x0, 0xF0) == 0x0, "bits past the fourth port are dropped");

    // The count stops at the first gap in the effective ports.
    Check(ControllerCount(EffectiveConnectedPorts(0x1, 0x4)) == 1, "port 3 forced behind a gap is not counted");
    Check(ControllerCount(EffectiveConnectedPorts(0x1, 0x6)) == 3, "forcing 2 and 3 fills the gap");
    Check(ControllerCount(EffectiveConnectedPorts(0x0, 0x8)) == 0, "port 4 alone is not counted");
    Check(ControllerCount(EffectiveConnectedPorts(0x0, 0xF)) == 4, "four forced ports make four players");
    Check(ControllerCount(EffectiveConnectedPorts(0x5, 0x2)) == 3, "forcing the gap joins ports 1 and 3");
    Check(ControllerCount(EffectiveConnectedPorts(0x0, 0x7)) == 3, "three forced ports make three players");
    Check(ControllerCount(EffectiveConnectedPorts(0x0, 0xD)) == 1, "a forced gap at port 2 stops at one");
    ForcedPorts() = 0x3;
    ConnectedPorts() = EffectiveConnectedPorts(0, ForcedPorts());
    Check(ControllerCount() == 2, "the game's count follows the forced ports");
    ForcedPorts() = 0;
    ConnectedPorts() = 0;
}

void TestKeys() {
    const PortDevice colon{ DeviceKind::Gamepad, "Pad: 1", 12 };
    for (const PortDevice& device : { PortDevice{}, kNone, kKbm, kPadA, colon }) {
        Check(ParseDeviceKey(DeviceKey(device)) == device, "a device survives its key");
    }
    Check(DeviceKey(PortDevice{}).empty(), "the default bindings are the empty key");
    for (const char* junk : { "garbage", "pad:", "pad:x:Name", "pad:1", "pad:-1:Name", "pad:1234567:Name", "KBM" }) {
        Check(ParseDeviceKey(junk).kind == DeviceKind::Bindings, "an unrecognized key reads as default bindings");
    }
    Check(ParseDeviceKey("pad:0:").kind == DeviceKind::Gamepad, "a gamepad with an empty name still parses");
}

void TestFind() {
    const AttachedDevices twins{ { { 3, "Twin" }, { 5, "Other" }, { 8, "Twin" } }, false };
    Check(FindGamepad(PortDevice{ DeviceKind::Gamepad, "Twin", 0 }, twins) == 0u, "the first twin");
    Check(FindGamepad(PortDevice{ DeviceKind::Gamepad, "Twin", 1 }, twins) == 2u, "the second twin");
    Check(!FindGamepad(PortDevice{ DeviceKind::Gamepad, "Twin", 2 }, twins), "no third twin");
    Check(!FindGamepad(kPadA, twins), "an unknown name");
    Check(!FindGamepad(kKbm, twins), "only gamepads are found");
    Check(IsAttached(PortDevice{}, {}) && IsAttached(kNone, {}), "default bindings and None are always there");
    Check(IsAttached(kKbm, kBoth) && !IsAttached(kKbm, AttachedDevices{ kBoth.gamepads, false }),
          "Keyboard & Mouse follows its presence");
}

void TestRoute() {
    // Unset ports leave the bindings alone; port 1 keeps every gamepad.
    {
        const auto routes = Route({}, kBoth);
        Check(!routes[0].managed && routes[0].gamepads == Ids({ 4, 7 }), "port 1 reads every gamepad by default");
        Check(routes[0].keyboard == KeyboardRoute::Unchanged, "and keeps its keyboard and mouse bindings");
        for (int port = 1; port < kPortCount; port++) {
            Check(!routes[port].managed && routes[port].gamepads.empty() &&
                      routes[port].keyboard == KeyboardRoute::Unchanged,
                  "other ports read no gamepad by default");
        }
    }

    // Any device can be assigned to any port, 3 and 4 included.
    {
        const PortDevices devices = { kPadB, kKbm, kPadA, kNone };
        const auto routes = Route(devices, kBoth);
        Check(routes[0].managed && routes[0].gamepads == Ids({ 7 }) && routes[0].keyboard == KeyboardRoute::Off,
              "port 1 reads only the second gamepad");
        Check(routes[1].managed && routes[1].gamepads.empty() && routes[1].keyboard == KeyboardRoute::On,
              "port 2 reads the keyboard and mouse");
        Check(routes[2].managed && routes[2].gamepads == Ids({ 4 }) && routes[2].keyboard == KeyboardRoute::Off,
              "port 3 reads the first gamepad");
        Check(routes[3].managed && routes[3].gamepads.empty() && routes[3].keyboard == KeyboardRoute::Off,
              "port 4 set to None reads nothing");
    }

    // A device that isn't attached feeds nothing, and one device can feed several ports.
    {
        const PortDevices missing = { PortDevice{}, PortDevice{ DeviceKind::Gamepad, "Pad C", 0 }, kKbm, PortDevice{} };
        const auto routes = Route(missing, AttachedDevices{ kBoth.gamepads, false });
        Check(routes[1].managed && routes[1].gamepads.empty() && routes[1].keyboard == KeyboardRoute::Off,
              "a missing gamepad reads nothing");
        Check(routes[2].managed && routes[2].keyboard == KeyboardRoute::Off,
              "a missing Keyboard & Mouse reads nothing");
        Check(Route(missing, AttachedDevices{ { { 2, "Pad C" } }, true })[1].gamepads == Ids({ 2 }),
              "until it is attached");
        const PortDevices shared = { kPadA, kPadA, kPadA, kPadA };
        for (int port = 0; port < kPortCount; port++) {
            Check(Route(shared, kBoth)[port].gamepads == Ids({ 4 }), "one gamepad can drive all four ports");
        }
        Check(Route({}, {})[0].gamepads.empty(), "no gamepads attached");
    }

    // Identical gamepads keep their order, so the same pick follows the same pad.
    {
        const PortDevice second{ DeviceKind::Gamepad, "Twin", 1 };
        const AttachedDevices before{ { { 3, "Twin" }, { 8, "Twin" } }, false };
        const AttachedDevices after{ { { 8, "Twin" } }, false };
        Check(Route({ second }, before)[0].gamepads == Ids({ 8 }), "the second twin is the later id");
        Check(Route({ second }, after)[0].gamepads.empty(), "and reads nothing once only one twin is left");
    }
}

void TestChoices() {
    const DeviceChoices choices = BuildDeviceChoices({}, kBoth);
    Check(choices.choices.size() == 5 && choices.selected == 0, "defaults, None, Keyboard & Mouse, two gamepads");
    Check(choices.choices[0].label == "Default bindings" && choices.choices[1].label == "None" &&
              choices.choices[2].label == "Keyboard & Mouse" && choices.choices[3].label == "Pad A" &&
              choices.choices[4].label == "Pad B",
          "entries in order");
    Check(choices.choices[4].device == kPadB, "an entry carries its device");
    Check(BuildDeviceChoices(kPadB, kBoth).selected == 4, "the selected device is marked");
    Check(BuildDeviceChoices(kKbm, kBoth).selected == 2, "Keyboard & Mouse can be selected");

    const DeviceChoices noKbm = BuildDeviceChoices({}, AttachedDevices{ kBoth.gamepads, false });
    Check(noKbm.choices.size() == 4 && noKbm.choices[2].label == "Pad A", "no Keyboard & Mouse entry without both");
    Check(BuildDeviceChoices({}, {}).choices.size() == 2, "nothing attached leaves default bindings and None");

    const DeviceChoices twins = BuildDeviceChoices({}, AttachedDevices{ { { 1, "Twin" }, { 2, "Twin" } }, false });
    Check(twins.choices[2].label == "Twin" && twins.choices[3].label == "Twin (2)", "identical names are numbered");
    Check(twins.choices[3].device.occurrence == 1, "and keep their place");
}

void TestDisconnected() {
    const AttachedDevices noPads{ {}, false };
    const DeviceChoices gone = BuildDeviceChoices(kPadA, noPads);
    Check(gone.choices.size() == 3 && gone.selected == 2 && gone.choices[2].label == "Pad A (disconnected)" &&
              gone.choices[2].device == kPadA,
          "a missing gamepad is listed as disconnected");
    const DeviceChoices goneKbm = BuildDeviceChoices(kKbm, noPads);
    Check(goneKbm.choices[goneKbm.selected].label == "Keyboard & Mouse (disconnected)", "so is Keyboard & Mouse");
    const PortDevice twin{ DeviceKind::Gamepad, "Twin", 1 };
    Check(DeviceLabel(twin, noPads) == "Twin (2) (disconnected)", "a numbered twin keeps its number");
    Check(DeviceLabel(PortDevice{ DeviceKind::Gamepad, "", 0 }, kBoth) == "Gamepad (disconnected)",
          "an unnamed gamepad");
    Check(DeviceLabel(kPadA, kBoth) == "Pad A", "an attached device has no suffix");
    Check(DisconnectedReason(kPadA, kBoth).empty() && DisconnectedReason({}, noPads).empty(),
          "attached means no reason");
    const std::string reason = DisconnectedReason(kPadA, noPads);
    Check(reason.find("Pad A") != std::string::npos && reason.find("no input") != std::string::npos &&
              reason.find("(disconnected)") == std::string::npos,
          "the reason names the device and what happens");
}

void TestDuplicateWarning() {
    Check(DuplicateDeviceWarning({}, kBoth, 1).empty(), "default ports don't warn");
    Check(DuplicateDeviceWarning({ kPadA, kPadB, kKbm, kNone }, kBoth, 0).empty(), "distinct devices don't warn");
    const PortDevices shared = { kPadA, kPadB, kPadA, kPadA };
    Check(DuplicateDeviceWarning(shared, kBoth, 0) == "Also used by port 3 and port 4.", "two other ports");
    Check(DuplicateDeviceWarning(shared, kBoth, 2) == "Also used by port 1 and port 4.", "from another port");
    Check(DuplicateDeviceWarning(shared, kBoth, 1).empty(), "the odd one out is clear");
    const PortDevices three = { kKbm, kKbm, kKbm, kPadA };
    Check(DuplicateDeviceWarning(three, kBoth, 1) == "Also used by port 1 and port 3.",
          "Keyboard & Mouse shared by three");
    Check(DuplicateDeviceWarning({ kNone, kNone }, kBoth, 0).empty(), "None is not a device");

    // Port 1 on the default bindings reads everything attached.
    const PortDevices underDefault = { PortDevice{}, kPadB, PortDevice{}, PortDevice{} };
    Check(DuplicateDeviceWarning(underDefault, kBoth, 1) == "Also used by port 1 (default bindings).",
          "port 1's default bindings read the chosen gamepad");
    Check(DuplicateDeviceWarning(underDefault, kBoth, 0).empty(), "a default port doesn't warn itself");
    const PortDevices kbmUnderDefault = { PortDevice{}, kKbm, PortDevice{}, PortDevice{} };
    Check(!DuplicateDeviceWarning(kbmUnderDefault, kBoth, 1).empty(), "and the keyboard and mouse");
    Check(DuplicateDeviceWarning(kbmUnderDefault, AttachedDevices{ kBoth.gamepads, false }, 1).empty(),
          "unless they aren't attached");
    Check(DuplicateDeviceWarning({ PortDevice{}, kPadB }, {}, 1).empty(), "nothing attached for port 1 to read");

    // A missing device still counts against the port that shares it.
    const AttachedDevices noPads{ {}, false };
    Check(DuplicateDeviceWarning({ kPadA, kPadA }, noPads, 0) == "Also used by port 2.",
          "two ports on a missing gamepad");
    Check(DuplicateDeviceWarning({ kPadA, kPadB }, noPads, 0).empty(), "different missing gamepads");

    // Identical pads are different devices.
    const PortDevice t0{ DeviceKind::Gamepad, "Twin", 0 };
    const PortDevice t1{ DeviceKind::Gamepad, "Twin", 1 };
    const AttachedDevices twins{ { { 1, "Twin" }, { 2, "Twin" } }, false };
    Check(DuplicateDeviceWarning({ t0, t1 }, twins, 0).empty(), "identical pads are different devices");
    Check(DuplicateDeviceWarning({ t0, t1 }, AttachedDevices{ { { 1, "Twin" } }, false }, 0).empty(),
          "the second twin gone is a different device from the first");
    Check(DuplicateDeviceWarning({ t0, t0 }, twins, 0) == "Also used by port 2.", "the same twin twice");
    Check(DuplicateDeviceWarning({ kPadA }, kBoth, -1).empty() && DuplicateDeviceWarning({ kPadA }, kBoth, 9).empty(),
          "a port out of range has no warning");
}

void TestHotplugNotice() {
    const PortDevices none{};
    Check(HotplugNotice(kBoth, kBoth, none).empty(), "no change, no notice");
    const AttachedDevices one{ { { 4, "Pad A" } }, true };
    const std::string unplugged = HotplugNotice(kBoth, one, none);
    Check(unplugged.find("Disconnected: Pad B.") != std::string::npos &&
              unplugged.find("Connected") == std::string::npos,
          "an unplug is named");
    const std::string plugged = HotplugNotice(one, kBoth, none);
    Check(plugged.find("Connected: Pad B.") != std::string::npos && plugged.find("Disconnected") == std::string::npos,
          "a plug-in is named");
    const std::string kbm = HotplugNotice(one, AttachedDevices{ one.gamepads, false }, none);
    Check(kbm.find("Disconnected: Keyboard & Mouse.") != std::string::npos, "so is Keyboard & Mouse");

    // The ports that lose or regain their device are called out.
    const PortDevices assigned = { kPadA, kPadB, PortDevice{}, kKbm };
    const std::string lost = HotplugNotice(kBoth, AttachedDevices{ { { 4, "Pad A" } }, false }, assigned);
    Check(lost.find("Pad B is disconnected, so port 2 reads no input.") != std::string::npos &&
              lost.find("Keyboard & Mouse is disconnected, so port 4 reads no input.") != std::string::npos &&
              lost.find("port 1") == std::string::npos,
          "ports that lost their device are named");
    const std::string back = HotplugNotice(AttachedDevices{ { { 4, "Pad A" } }, false }, kBoth, assigned);
    Check(back.find("Pad B is back on port 2.") != std::string::npos &&
              back.find("Keyboard & Mouse is back on port 4.") != std::string::npos,
          "and when they return");

    // Identical pads renumber when the earlier one is unplugged.
    const PortDevice first{ DeviceKind::Gamepad, "Twin", 0 };
    const PortDevice second{ DeviceKind::Gamepad, "Twin", 1 };
    const AttachedDevices twins{ { { 3, "Twin" }, { 8, "Twin" } }, false };
    const AttachedDevices left{ { { 8, "Twin" } }, false };
    const std::string renumbered = HotplugNotice(twins, left, PortDevices{ first, second });
    Check(renumbered.find("port 1 now reads a different Twin") != std::string::npos &&
              renumbered.find("Twin (2) is disconnected, so port 2 reads no input.") != std::string::npos,
          "renumbered twins are called out");
    // A new pad that sorts after the picks changes nothing for them.
    const AttachedDevices more{ { { 3, "Twin" }, { 8, "Twin" }, { 9, "Pad A" } }, false };
    const std::string added = HotplugNotice(twins, more, PortDevices{ first, second });
    Check(added.find("Connected: Pad A.") != std::string::npos && added.find("port") == std::string::npos,
          "ports whose device didn't move aren't mentioned");
}

void TestAutoPick() {
    const AttachedDevices kbmOnly{ {}, true };
    const AttachedDevices padOnly{ { { 4, "Pad A" }, { 7, "Pad B" } }, false };
    const AttachedDevices nothing{};
    const AutoPickState fresh{};

    AutoPick pick = DecideAutoPick(fresh, kBoth);
    Check(pick.action == AutoPickAction::Pick && pick.device == kKbm, "fresh install with K&M and pads picks K&M");
    pick = DecideAutoPick(fresh, kbmOnly);
    Check(pick.action == AutoPickAction::Pick && pick.device == kKbm, "fresh install with K&M picks K&M");
    pick = DecideAutoPick(fresh, padOnly);
    Check(pick.action == AutoPickAction::Pick && pick.device == kPadA, "fresh install with pads picks the first");
    const AttachedDevices twins{ { { 3, "Twin" }, { 8, "Twin" } }, false };
    pick = DecideAutoPick(fresh, twins);
    Check(pick.action == AutoPickAction::Pick && pick.device == PortDevice{ DeviceKind::Gamepad, "Twin", 0 },
          "twin pads pick occurrence 0");

    // Nothing attached waits; a later hot-plug then picks.
    Check(DecideAutoPick(fresh, nothing).action == AutoPickAction::Wait, "nothing attached waits");
    Check(DecideAutoPick(fresh, padOnly).action == AutoPickAction::Pick, "a hot-plugged pad is picked afterwards");

    AutoPickState custom;
    custom.customBindings = true;
    Check(DecideAutoPick(custom, kBoth).action == AutoPickAction::Finish, "custom mappings are left alone");
    Check(DecideAutoPick(custom, nothing).action == AutoPickAction::Finish, "custom mappings finish with no device");
    AutoPickState chosen;
    chosen.deviceChosen = true;
    Check(DecideAutoPick(chosen, kBoth).action == AutoPickAction::Finish, "a saved device choice is left alone");
    AutoPickState done;
    done.done = true;
    Check(DecideAutoPick(done, kBoth).action == AutoPickAction::None, "an explicit Default or None choice stays");
}

} // namespace

int main() {
    TestConnectedPorts();
    TestKeys();
    TestFind();
    TestRoute();
    TestChoices();
    TestDisconnected();
    TestDuplicateWarning();
    TestHotplugNotice();
    TestAutoPick();
    return gFailures == 0 ? 0 : 1;
}
