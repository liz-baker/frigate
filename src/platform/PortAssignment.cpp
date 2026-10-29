#include "platform/PortAssignment.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace Frigate::Platform::Input {

namespace {

constexpr const char* kKeyNone = "none";
constexpr const char* kKeyKeyboardMouse = "kbm";
constexpr const char* kKeyGamepadPrefix = "pad:";

std::string PortName(int port) {
    return "port " + std::to_string(port + 1);
}

std::string JoinList(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) {
            out += i + 1 == items.size() ? " and " : ", ";
        }
        out += items[i];
    }
    return out;
}

std::string GamepadLabel(const std::string& name, int occurrence) {
    std::string label = name.empty() ? "Gamepad" : name;
    if (occurrence > 0) {
        label += " (" + std::to_string(occurrence + 1) + ")";
    }
    return label;
}

std::string BaseLabel(const PortDevice& device) {
    switch (device.kind) {
        case DeviceKind::Bindings:
            return "Default bindings";
        case DeviceKind::None:
            return "None";
        case DeviceKind::KeyboardMouse:
            return "Keyboard & Mouse";
        case DeviceKind::Gamepad:
            return GamepadLabel(device.name, device.occurrence);
    }
    return {};
}

// What `device` on `port` reads right now, as identifiers comparable across
// ports. A device that isn't attached is identified by its key, so two ports
// pointing at the same missing device still match.
std::set<std::string> DevicesRead(const PortDevices& devices, const AttachedDevices& attached, int port) {
    std::set<std::string> read;
    const PortDevice& device = devices[static_cast<size_t>(port)];
    switch (device.kind) {
        case DeviceKind::Bindings:
            if (port == 0) {
                for (const Gamepad& pad : attached.gamepads) {
                    read.insert("pad#" + std::to_string(pad.id));
                }
                if (attached.keyboardMouse) {
                    read.insert(kKeyKeyboardMouse);
                }
            }
            break;
        case DeviceKind::None:
            break;
        case DeviceKind::KeyboardMouse:
            read.insert(kKeyKeyboardMouse);
            break;
        case DeviceKind::Gamepad:
            if (const auto index = FindGamepad(device, attached)) {
                read.insert("pad#" + std::to_string(attached.gamepads[*index].id));
            } else {
                read.insert(DeviceKey(device));
            }
            break;
    }
    return read;
}

} // namespace

std::string DeviceKey(const PortDevice& device) {
    switch (device.kind) {
        case DeviceKind::Bindings:
            return {};
        case DeviceKind::None:
            return kKeyNone;
        case DeviceKind::KeyboardMouse:
            return kKeyKeyboardMouse;
        case DeviceKind::Gamepad:
            return kKeyGamepadPrefix + std::to_string(device.occurrence) + ":" + device.name;
    }
    return {};
}

PortDevice ParseDeviceKey(const std::string& key) {
    PortDevice device;
    if (key == kKeyNone) {
        device.kind = DeviceKind::None;
    } else if (key == kKeyKeyboardMouse) {
        device.kind = DeviceKind::KeyboardMouse;
    } else if (key.rfind(kKeyGamepadPrefix, 0) == 0) {
        const size_t digitsStart = std::char_traits<char>::length(kKeyGamepadPrefix);
        const size_t colon = key.find(':', digitsStart);
        if (colon != std::string::npos && colon > digitsStart && colon - digitsStart <= 6 &&
            key.find_first_not_of("0123456789", digitsStart) == colon) {
            device.kind = DeviceKind::Gamepad;
            device.occurrence = std::stoi(key.substr(digitsStart, colon - digitsStart));
            device.name = key.substr(colon + 1);
        }
    }
    return device;
}

std::optional<size_t> FindGamepad(const PortDevice& device, const AttachedDevices& attached) {
    if (device.kind != DeviceKind::Gamepad) {
        return std::nullopt;
    }
    int seen = 0;
    for (size_t i = 0; i < attached.gamepads.size(); i++) {
        if (attached.gamepads[i].name == device.name && seen++ == device.occurrence) {
            return i;
        }
    }
    return std::nullopt;
}

bool IsAttached(const PortDevice& device, const AttachedDevices& attached) {
    switch (device.kind) {
        case DeviceKind::Bindings:
        case DeviceKind::None:
            return true;
        case DeviceKind::KeyboardMouse:
            return attached.keyboardMouse;
        case DeviceKind::Gamepad:
            return FindGamepad(device, attached).has_value();
    }
    return true;
}

std::array<PortRoute, kPortCount> Route(const PortDevices& devices, const AttachedDevices& attached) {
    std::array<PortRoute, kPortCount> routes;
    for (int port = 0; port < kPortCount; port++) {
        PortRoute& route = routes[static_cast<size_t>(port)];
        const PortDevice& device = devices[static_cast<size_t>(port)];
        if (device.kind == DeviceKind::Bindings) {
            if (port == 0) {
                for (const Gamepad& pad : attached.gamepads) {
                    route.gamepads.push_back(pad.id);
                }
            }
            continue;
        }
        route.managed = true;
        route.keyboard =
            device.kind == DeviceKind::KeyboardMouse && attached.keyboardMouse ? KeyboardRoute::On : KeyboardRoute::Off;
        if (const auto index = FindGamepad(device, attached)) {
            route.gamepads.push_back(attached.gamepads[*index].id);
        }
    }
    return routes;
}

uint8_t EffectiveConnectedPorts(uint8_t detected, uint8_t forced) {
    return static_cast<uint8_t>((detected | forced) & ((1 << kPortCount) - 1));
}

std::string DeviceLabel(const PortDevice& device, const AttachedDevices& attached) {
    std::string label = BaseLabel(device);
    if (!IsAttached(device, attached)) {
        label += " (disconnected)";
    }
    return label;
}

DeviceChoices BuildDeviceChoices(const PortDevice& selected, const AttachedDevices& attached) {
    DeviceChoices result;
    auto& choices = result.choices;
    choices.push_back({ PortDevice{ DeviceKind::Bindings, {}, 0 }, "Default bindings" });
    choices.push_back({ PortDevice{ DeviceKind::None, {}, 0 }, "None" });
    if (attached.keyboardMouse) {
        choices.push_back({ PortDevice{ DeviceKind::KeyboardMouse, {}, 0 }, "Keyboard & Mouse" });
    }
    std::map<std::string, int> seen;
    for (const Gamepad& pad : attached.gamepads) {
        const int occurrence = seen[pad.name]++;
        choices.push_back(
            { PortDevice{ DeviceKind::Gamepad, pad.name, occurrence }, GamepadLabel(pad.name, occurrence) });
    }
    const auto found = std::find_if(choices.begin(), choices.end(),
                                    [&](const DeviceChoice& choice) { return choice.device == selected; });
    if (found == choices.end()) {
        choices.push_back({ selected, DeviceLabel(selected, attached) });
        result.selected = choices.size() - 1;
    } else {
        result.selected = static_cast<size_t>(found - choices.begin());
    }
    return result;
}

std::string DuplicateDeviceWarning(const PortDevices& devices, const AttachedDevices& attached, int port) {
    if (port < 0 || port >= kPortCount) {
        return {};
    }
    const DeviceKind kind = devices[static_cast<size_t>(port)].kind;
    if (kind == DeviceKind::Bindings || kind == DeviceKind::None) {
        return {};
    }
    const std::set<std::string> mine = DevicesRead(devices, attached, port);
    std::vector<std::string> others;
    for (int other = 0; other < kPortCount; other++) {
        if (other == port) {
            continue;
        }
        const std::set<std::string> theirs = DevicesRead(devices, attached, other);
        const bool shared =
            std::any_of(mine.begin(), mine.end(), [&](const std::string& id) { return theirs.count(id) != 0; });
        if (shared) {
            std::string name = PortName(other);
            if (devices[static_cast<size_t>(other)].kind == DeviceKind::Bindings) {
                name += " (default bindings)";
            }
            others.push_back(std::move(name));
        }
    }
    if (others.empty()) {
        return {};
    }
    return "Also used by " + JoinList(others) + ".";
}

std::string DisconnectedReason(const PortDevice& device, const AttachedDevices& attached) {
    if (IsAttached(device, attached)) {
        return {};
    }
    return "Its device, " + BaseLabel(device) +
           ", is disconnected, so this port reads no input. Plug it back in or choose another device.";
}

std::string HotplugNotice(const AttachedDevices& before, const AttachedDevices& after, const PortDevices& devices) {
    if (before == after) {
        return {};
    }
    const auto hasId = [](const AttachedDevices& set, int32_t id) {
        return std::any_of(set.gamepads.begin(), set.gamepads.end(), [id](const Gamepad& pad) { return pad.id == id; });
    };
    std::vector<std::string> added;
    std::vector<std::string> removed;
    for (const Gamepad& pad : after.gamepads) {
        if (!hasId(before, pad.id)) {
            added.push_back(GamepadLabel(pad.name, 0));
        }
    }
    for (const Gamepad& pad : before.gamepads) {
        if (!hasId(after, pad.id)) {
            removed.push_back(GamepadLabel(pad.name, 0));
        }
    }
    if (after.keyboardMouse && !before.keyboardMouse) {
        added.push_back("Keyboard & Mouse");
    } else if (!after.keyboardMouse && before.keyboardMouse) {
        removed.push_back("Keyboard & Mouse");
    }

    std::string notice = "Devices changed.";
    if (!added.empty()) {
        notice += " Connected: " + JoinList(added) + ".";
    }
    if (!removed.empty()) {
        notice += " Disconnected: " + JoinList(removed) + ".";
    }
    for (int port = 0; port < kPortCount; port++) {
        const PortDevice& device = devices[static_cast<size_t>(port)];
        if (device.kind != DeviceKind::Gamepad && device.kind != DeviceKind::KeyboardMouse) {
            continue;
        }
        const bool wasThere = IsAttached(device, before);
        const bool isThere = IsAttached(device, after);
        const std::string label = BaseLabel(device);
        std::string line;
        if (wasThere && !isThere) {
            line = label + " is disconnected, so " + PortName(port) + " reads no input.";
        } else if (!wasThere && isThere) {
            line = label + " is back on " + PortName(port) + ".";
        } else if (wasThere && isThere && device.kind == DeviceKind::Gamepad &&
                   before.gamepads[*FindGamepad(device, before)].id != after.gamepads[*FindGamepad(device, after)].id) {
            line = PortName(port) + " now reads a different " + label +
                   ": controllers with the same name were renumbered.";
        }
        if (!line.empty()) {
            notice += "\n" + line;
        }
    }
    return notice;
}

PortDevices& Devices() {
    static PortDevices devices{};
    return devices;
}

uint8_t& ForcedPorts() {
    static uint8_t forced = 0;
    return forced;
}

uint8_t& SessionForcedPorts() {
    static uint8_t forced = 0;
    return forced;
}

std::string& DeviceNotice() {
    static std::string notice;
    return notice;
}

AutoPick DecideAutoPick(const AutoPickState& state, const AttachedDevices& attached) {
    if (state.done) {
        return { AutoPickAction::None, {} };
    }
    if (state.deviceChosen || state.customBindings) {
        return { AutoPickAction::Finish, {} };
    }
    if (attached.keyboardMouse) {
        return { AutoPickAction::Pick, { DeviceKind::KeyboardMouse, "", 0 } };
    }
    if (!attached.gamepads.empty()) {
        return { AutoPickAction::Pick, { DeviceKind::Gamepad, attached.gamepads.front().name, 0 } };
    }
    return { AutoPickAction::Wait, {} };
}

} // namespace Frigate::Platform::Input
