#include "platform/PortAssignment.h"

#include <cstdio>

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

} // namespace

int main() {
    // A forced port joins the detected ones; ports 3 and 4 count like port 2.
    {
        Check(EffectiveConnectedPorts(0x0, 0x0) == 0x0, "nothing detected or forced");
        Check(EffectiveConnectedPorts(0x1, 0x2) == 0x3, "a forced port 2 joins port 1");
        Check(EffectiveConnectedPorts(0x0, 0xF) == 0xF, "all four forced with nothing attached");
        Check(EffectiveConnectedPorts(0x3, 0x3) == 0x3, "forcing an attached port changes nothing");
        Check(EffectiveConnectedPorts(0x0, 0xF0) == 0x0, "bits past the fourth port are dropped");
    }

    // The count stops at the first gap in the effective ports.
    {
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

    // Unset ports leave the bindings alone; port 1 keeps every gamepad.
    {
        const PortDevices codes{};
        const auto routes = Route(codes, 2);
        Check(!routes[0].managed && routes[0].gamepads == 0x3, "port 1 reads every gamepad by default");
        Check(routes[0].keyboard == KeyboardRoute::Unchanged, "and keeps its keyboard bindings");
        for (int port = 1; port < kPortCount; port++) {
            Check(!routes[port].managed && routes[port].gamepads == 0, "other ports read no gamepad by default");
        }
    }

    // A keyboard or gamepad can be assigned to any port, 3 and 4 included.
    {
        const PortDevices codes = { GamepadCode(1), kDeviceKeyboard, GamepadCode(0), kDeviceNone };
        const auto routes = Route(codes, 2);
        Check(routes[0].managed && routes[0].gamepads == 0x2 && routes[0].keyboard == KeyboardRoute::Off,
              "port 1 reads only the second gamepad");
        Check(routes[1].managed && routes[1].gamepads == 0 && routes[1].keyboard == KeyboardRoute::On,
              "port 2 reads the keyboard");
        Check(routes[2].managed && routes[2].gamepads == 0x1 && routes[2].keyboard == KeyboardRoute::Off,
              "port 3 reads the first gamepad");
        Check(routes[3].managed && routes[3].gamepads == 0 && routes[3].keyboard == KeyboardRoute::Off,
              "port 4 set to None reads nothing");
    }

    // A gamepad that isn't attached feeds nothing, and one gamepad can feed two ports.
    {
        const PortDevices missing = { kDeviceBindings, GamepadCode(2), kDeviceBindings, kDeviceBindings };
        Check(Route(missing, 2)[1].gamepads == 0 && Route(missing, 2)[1].managed, "a missing gamepad reads nothing");
        Check(Route(missing, 3)[1].gamepads == 0x4, "until it is attached");
        const PortDevices shared = { GamepadCode(0), GamepadCode(0), GamepadCode(0), GamepadCode(0) };
        for (int port = 0; port < kPortCount; port++) {
            Check(Route(shared, 1)[port].gamepads == 0x1, "one gamepad can drive all four ports");
        }
        Check(Route({}, 0)[0].gamepads == 0, "no gamepads attached");
        Check(Route({}, 40)[0].gamepads == 0xFFFFFFFFu, "the gamepad mask saturates");
    }

    // Swapping exchanges two ports' devices and ignores bad ports.
    {
        PortDevices codes = { GamepadCode(0), kDeviceKeyboard, kDeviceNone, kDeviceBindings };
        SwapPorts(codes, 0, 1);
        Check(codes[0] == kDeviceKeyboard && codes[1] == GamepadCode(0), "ports 1 and 2 swap");
        SwapPorts(codes, 2, 3);
        Check(codes[2] == kDeviceBindings && codes[3] == kDeviceNone, "ports 3 and 4 swap");
        SwapPorts(codes, 0, 3);
        Check(codes[0] == kDeviceNone && codes[3] == kDeviceKeyboard, "a distant pair swaps");
        const PortDevices before = codes;
        SwapPorts(codes, 1, 1);
        SwapPorts(codes, -1, 2);
        SwapPorts(codes, 0, kPortCount);
        Check(codes == before, "equal or out-of-range ports do nothing");
    }

    // The combo's labels line up with the codes.
    {
        const auto labels = DeviceLabels({}, { "Pad A", "Pad B" });
        Check(labels.size() == 5, "fixed choices plus a label per gamepad");
        Check(labels[kDeviceBindings] == "Bindings" && labels[kDeviceNone] == "None" &&
                  labels[kDeviceKeyboard] == "Keyboard",
              "fixed labels sit at their codes");
        Check(labels[GamepadCode(1)] == "Gamepad 2: Pad B", "a gamepad's label sits at its code");
        const PortDevices stale = { kDeviceBindings, GamepadCode(3), kDeviceBindings, kDeviceBindings };
        const auto kept = DeviceLabels(stale, { "Pad A" });
        Check(kept.size() == 3 + 4 && kept[GamepadCode(3)] == "Gamepad 4 (not connected)",
              "a saved gamepad that is gone stays selectable");
        Check(DeviceLabels({}, {}).size() == 3, "no gamepads, three choices");
    }

    return gFailures == 0 ? 0 : 1;
}
