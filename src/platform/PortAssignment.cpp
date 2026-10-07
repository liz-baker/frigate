#include "platform/PortAssignment.h"

#include <algorithm>
#include <utility>

namespace Frigate::Platform::Input {

std::array<PortRoute, kPortCount> Route(const PortDevices& codes, int gamepadCount) {
    std::array<PortRoute, kPortCount> routes;
    const int attached = std::clamp(gamepadCount, 0, 32);
    const uint32_t all = attached >= 32 ? 0xFFFFFFFFu : (1u << attached) - 1;
    for (int port = 0; port < kPortCount; port++) {
        PortRoute& route = routes[port];
        const int code = codes[port];
        if (code < kDeviceNone) {
            route.gamepads = port == 0 ? all : 0;
            continue;
        }
        route.managed = true;
        route.keyboard = code == kDeviceKeyboard ? KeyboardRoute::On : KeyboardRoute::Off;
        const int ordinal = code - kDeviceGamepadBase;
        if (ordinal >= 0 && ordinal < attached) {
            route.gamepads = 1u << ordinal;
        }
    }
    return routes;
}

uint8_t EffectiveConnectedPorts(uint8_t detected, uint8_t forced) {
    return static_cast<uint8_t>((detected | forced) & ((1 << kPortCount) - 1));
}

void SwapPorts(PortDevices& codes, int a, int b) {
    if (a == b || a < 0 || b < 0 || a >= kPortCount || b >= kPortCount) {
        return;
    }
    std::swap(codes[a], codes[b]);
}

std::vector<std::string> DeviceLabels(const PortDevices& codes, const std::vector<std::string>& gamepadNames) {
    std::vector<std::string> labels = { "Bindings", "None", "Keyboard" };
    size_t count = gamepadNames.size();
    for (const int code : codes) {
        if (code >= kDeviceGamepadBase) {
            count = std::max(count, static_cast<size_t>(code - kDeviceGamepadBase + 1));
        }
    }
    for (size_t i = 0; i < count; i++) {
        std::string label = "Gamepad " + std::to_string(i + 1);
        if (i < gamepadNames.size()) {
            label += ": " + gamepadNames[i];
        } else {
            label += " (not connected)";
        }
        labels.push_back(std::move(label));
    }
    return labels;
}

PortDevices& Devices() {
    static PortDevices devices{};
    return devices;
}

uint8_t& ForcedPorts() {
    static uint8_t forced = 0;
    return forced;
}

} // namespace Frigate::Platform::Input
