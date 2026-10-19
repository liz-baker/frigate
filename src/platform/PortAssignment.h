#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "platform/InputState.h"

// Which attached device feeds each controller port, and which ports count as
// connected without a device. Pure logic: the libultraship side that applies
// it is ApplyRouting in platform/Input.h.
namespace Frigate::Platform::Input {

enum class DeviceKind { Bindings, None, KeyboardMouse, Gamepad };

// What a port reads from, as stored in the config. A gamepad is identified by
// its name and its place among the attached gamepads sharing that name, since
// the ids the platform gives them don't survive a restart.
struct PortDevice {
    // Bindings leaves the port's bindings as they are (libultraship's defaults
    // put every gamepad and the keyboard on port 1 and nothing on the others).
    DeviceKind kind = DeviceKind::Bindings;
    std::string name;
    int occurrence = 0;

    bool operator==(const PortDevice& other) const {
        return kind == other.kind && name == other.name && occurrence == other.occurrence;
    }
    bool operator!=(const PortDevice& other) const {
        return !(*this == other);
    }
};

using PortDevices = std::array<PortDevice, kPortCount>;

// The config string for a device: "" (Bindings), "none", "kbm", or
// "pad:<occurrence>:<name>". An unrecognized string reads as Bindings.
std::string DeviceKey(const PortDevice& device);
PortDevice ParseDeviceKey(const std::string& key);

struct Gamepad {
    // The platform's id for the device: unique among the attached gamepads and
    // unchanged while it stays attached.
    int32_t id = 0;
    std::string name;

    bool operator==(const Gamepad& other) const {
        return id == other.id && name == other.name;
    }
};

// What is attached. The combined Keyboard & Mouse device exists only when both
// are present.
struct AttachedDevices {
    std::vector<Gamepad> gamepads;
    bool keyboardMouse = false;

    bool operator==(const AttachedDevices& other) const {
        return gamepads == other.gamepads && keyboardMouse == other.keyboardMouse;
    }
    bool operator!=(const AttachedDevices& other) const {
        return !(*this == other);
    }
};

// The attached gamepad `device` names, as an index into attached.gamepads.
std::optional<size_t> FindGamepad(const PortDevice& device, const AttachedDevices& attached);

// Whether the device can feed a port right now; always true for Bindings and None.
bool IsAttached(const PortDevice& device, const AttachedDevices& attached);

enum class KeyboardRoute { Unchanged, On, Off };

struct PortRoute {
    // Applies to the keyboard and the mouse together.
    KeyboardRoute keyboard = KeyboardRoute::Unchanged;
    // Ids of the gamepads that feed this port.
    std::vector<int32_t> gamepads;
    // The port has a device of its own choosing, so it needs bindings for it.
    bool managed = false;
};

// The routing for `devices` with `attached` present. A device that isn't
// attached feeds nothing.
std::array<PortRoute, kPortCount> Route(const PortDevices& devices, const AttachedDevices& attached);

// g_ConnectedControllers with the ports forced connected (bit n = port n)
// added to the ones a device was found for.
uint8_t EffectiveConnectedPorts(uint8_t detected, uint8_t forced);

// One entry of a port's device dropdown.
struct DeviceChoice {
    PortDevice device;
    std::string label;
};

struct DeviceChoices {
    std::vector<DeviceChoice> choices;
    // Index of the port's current device in choices.
    size_t selected = 0;
};

// The label for a device, with " (disconnected)" appended when it isn't attached.
std::string DeviceLabel(const PortDevice& device, const AttachedDevices& attached);

// The dropdown for a port whose device is `selected`: Default bindings, None,
// Keyboard & Mouse when both are attached, then every attached gamepad. A
// selected device that isn't attached is listed last as "<name> (disconnected)".
DeviceChoices BuildDeviceChoices(const PortDevice& selected, const AttachedDevices& attached);

// A sentence for the warning beside `port`'s dropdown naming the other ports
// that read a device it reads; empty when there are none, or when `port` is on
// Bindings or None. Port 1 on Bindings counts as reading every attached device.
std::string DuplicateDeviceWarning(const PortDevices& devices, const AttachedDevices& attached, int port);

// Why a port with `device` reads no input and its bindings can't be edited:
// the device isn't attached. Empty when it is.
std::string DisconnectedReason(const PortDevice& device, const AttachedDevices& attached);

// What changed between two snapshots of the attached devices, for the notice
// shown after hot-plugging: what was connected and disconnected, and each port
// whose device now resolves to a different one. Empty when nothing changed.
std::string HotplugNotice(const AttachedDevices& before, const AttachedDevices& after, const PortDevices& devices);

// The settings the menu edits and the game reads, set by the menu at startup
// and on each change.
PortDevices& Devices();
uint8_t& ForcedPorts();
// Ports forced connected for the game in progress rather than by the setting
// (a quick-started multiplayer game's players), which the menu's changes to
// ForcedPorts() leave alone.
uint8_t& SessionForcedPorts();
// The latest HotplugNotice the player hasn't dismissed.
std::string& DeviceNotice();

} // namespace Frigate::Platform::Input
