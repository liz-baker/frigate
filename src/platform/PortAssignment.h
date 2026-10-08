#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/InputState.h"

// Which attached device feeds each controller port, and which ports count as
// connected without a device. Pure logic: the libultraship side that applies
// it is ApplyRouting in platform/Input.h.
namespace Frigate::Platform::Input {

// What a port reads from, as stored in the config. Gamepads are numbered by
// their order among the attached ones, so the code survives a restart.
enum DeviceCode : int {
    // Leave the port's bindings as they are (libultraship's defaults put
    // every gamepad and the keyboard on port 1 and nothing on the others).
    kDeviceBindings = 0,
    kDeviceNone = 1,
    kDeviceKeyboard = 2,
    kDeviceGamepadBase = 3,
};

constexpr int GamepadCode(int ordinal) {
    return kDeviceGamepadBase + ordinal;
}

using PortDevices = std::array<int, kPortCount>;

enum class KeyboardRoute { Unchanged, On, Off };

struct PortRoute {
    KeyboardRoute keyboard = KeyboardRoute::Unchanged;
    // Bit n: the nth attached gamepad feeds this port.
    uint32_t gamepads = 0;
    // The port has a device of its own choosing, so it needs bindings for it.
    bool managed = false;
};

// The routing for `codes` with `gamepadCount` gamepads attached. A gamepad
// that isn't attached feeds nothing.
std::array<PortRoute, kPortCount> Route(const PortDevices& codes, int gamepadCount);

// g_ConnectedControllers with the ports forced connected (bit n = port n)
// added to the ones a device was found for.
uint8_t EffectiveConnectedPorts(uint8_t detected, uint8_t forced);

// Exchanges what ports `a` and `b` read from. Out-of-range or equal ports do
// nothing.
void SwapPorts(PortDevices& codes, int a, int b);

// The device choices, indexed by code: the fixed ones, then one per attached
// gamepad, then one per gamepad some port still points at that is gone.
std::vector<std::string> DeviceLabels(const PortDevices& codes, const std::vector<std::string>& gamepadNames);

// The settings the menu edits and the game reads, set by the menu at startup
// and on each change.
PortDevices& Devices();
uint8_t& ForcedPorts();
// Ports forced connected for the game in progress rather than by the setting
// (a quick-started multiplayer game's players), which the menu's changes to
// ForcedPorts() leave alone.
uint8_t& SessionForcedPorts();

} // namespace Frigate::Platform::Input
