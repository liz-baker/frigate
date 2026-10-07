#include "platform/Input.h"

#include <algorithm>
#include <cstring>

#include "libultraship/bridge/controllerbridge.h"
#include "libultraship/libultra/motor.h"
#include "libultraship/libultra/os.h"
#include "platform/Rumble.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/core/Context.h"

namespace Frigate::Platform::Input {

bool Init(const std::shared_ptr<Ship::Context>& context) {
    auto deck = context->GetChildren().GetFirst<Ship::ControlDeck>();
    if (deck == nullptr) {
        return false;
    }
    // osContInit finds the deck through this bridge cache.
    ControllerSetControlDeck(deck);
    // osContInit needs somewhere to write the ports connected at init; the
    // live ones come from ConnectedPortsOf.
    static uint8_t controllerBits = 0;
    OSContStatus status{};
    return osContInit(nullptr, &controllerBits, &status) == 0;
}

void ReadPads(Ship::ControlDeck* deck, OSContPad* pads) {
    std::memset(pads, 0, sizeof(OSContPad) * kPortCount);
    if (deck != nullptr) {
        deck->WriteToPad(pads);
    }
}

uint8_t DetectedPortsOf(Ship::ControlDeck* deck) {
    uint8_t ports = 0;
    if (deck == nullptr) {
        return ports;
    }
    for (int port = 0; port < kPortCount; port++) {
        const std::shared_ptr<Ship::Controller> controller = deck->GetControllerByPort(static_cast<uint8_t>(port));
        if (controller != nullptr && controller->IsConnected()) {
            ports |= static_cast<uint8_t>(1 << port);
        }
    }
    return ports;
}

uint8_t ConnectedPortsOf(Ship::ControlDeck* deck) {
    return EffectiveConnectedPorts(DetectedPortsOf(deck), ForcedPorts() | SessionForcedPorts());
}

namespace {

// Attached gamepad instance ids in the order the device codes number them.
std::vector<int32_t> GamepadIds(Ship::ControlDeck* deck) {
    std::vector<int32_t> ids;
    if (deck == nullptr) {
        return ids;
    }
    for (const auto& [id, name] : deck->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadNames()) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace

std::vector<std::string> GamepadNames(Ship::ControlDeck* deck) {
    std::vector<std::string> names;
    if (deck == nullptr) {
        return names;
    }
    const auto byId = deck->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadNames();
    for (const int32_t id : GamepadIds(deck)) {
        names.push_back(byId.at(id));
    }
    return names;
}

void ApplyRouting(Ship::ControlDeck* deck, const PortDevices& codes) {
    if (deck == nullptr) {
        return;
    }
    const std::vector<int32_t> ids = GamepadIds(deck);
    const auto routes = Route(codes, static_cast<int>(ids.size()));
    auto manager = deck->GetConnectedPhysicalDeviceManager();
    for (int port = 0; port < kPortCount; port++) {
        const PortRoute& route = routes[port];
        const auto portIndex = static_cast<uint8_t>(port);
        for (size_t i = 0; i < ids.size(); i++) {
            if (i < 32 && (route.gamepads >> i & 1) != 0) {
                manager->UnignoreInstanceIdForPort(portIndex, ids[i]);
            } else {
                manager->IgnoreInstanceIdForPort(portIndex, ids[i]);
            }
        }
        const std::shared_ptr<Ship::Controller> controller = deck->GetControllerByPort(portIndex);
        if (controller == nullptr) {
            continue;
        }
        if (route.keyboard == KeyboardRoute::Off) {
            controller->ClearAllMappingsForDeviceType(Ship::PhysicalDeviceType::Keyboard);
        } else if (route.keyboard == KeyboardRoute::On &&
                   !controller->HasMappingsForPhysicalDeviceType(Ship::PhysicalDeviceType::Keyboard)) {
            controller->AddDefaultMappings(Ship::PhysicalDeviceType::Keyboard);
        }
        if (route.managed && route.gamepads != 0 &&
            !controller->HasMappingsForPhysicalDeviceType(PHYSICAL_DEVICE_TYPE_GAMEPAD)) {
            controller->AddDefaultMappings(PHYSICAL_DEVICE_TYPE_GAMEPAD);
        }
    }
}

std::array<PadSample, kPortCount> SamplesFromPads(const OSContPad* pads) {
    std::array<PadSample, kPortCount> samples;
    for (int port = 0; port < kPortCount; port++) {
        samples[port].stickX = pads[port].stick_x;
        samples[port].stickY = pads[port].stick_y;
        samples[port].buttons = static_cast<uint16_t>(pads[port].button);
    }
    return samples;
}

// osMotorStart and osMotorStop on the controller of `port`.
bool DriveMotor(Ship::ControlDeck* deck, int port, bool on) {
    if (deck == nullptr || deck->GetControllerByPort(static_cast<uint8_t>(port)) == nullptr) {
        return false;
    }
    OSPfs pfs{};
    osMotorInit(nullptr, &pfs, port);
    return (on ? osMotorStart(&pfs) : osMotorStop(&pfs)) == 0;
}

namespace {

// What the deck was last pointed at, so a change in Devices() or in the
// attached gamepads (hot-plugging renumbers them) is applied once.
struct AppliedRouting {
    bool applied = false;
    PortDevices codes{};
    std::vector<int32_t> gamepads;
};

void SyncRouting(Ship::ControlDeck* deck) {
    static AppliedRouting last;
    const PortDevices& codes = Devices();
    const std::vector<int32_t> gamepads = GamepadIds(deck);
    const bool unmanaged = std::all_of(codes.begin(), codes.end(), [](int code) { return code < kDeviceNone; });
    if (deck == nullptr || (last.codes == codes && last.gamepads == gamepads) || (unmanaged && !last.applied)) {
        return;
    }
    ApplyRouting(deck, codes);
    last.applied = true;
    last.codes = codes;
    last.gamepads = gamepads;
}

} // namespace

void Poll(Ship::ControlDeck* deck) {
    SyncRouting(deck);
    OSContPad pads[kPortCount];
    ReadPads(deck, pads);
    std::array<PadSample, kPortCount> samples = SamplesFromPads(pads);
    uint8_t rumbleReady = DetectedPortsOf(deck);
    ConnectedPorts() = EffectiveConnectedPorts(rumbleReady, ForcedPorts() | SessionForcedPorts());
    if (const auto& script = Script()) {
        static uint32_t frame = 0;
        samples[0] = script->At(frame++);
        ConnectedPorts() |= 1;
        rumbleReady |= 1;
    }
    Advance(Current(), samples);

    // joyRumblePakTick.
    Rumble& rumble = GlobalRumble();
    rumble.SetReady(rumbleReady);
    rumble.Tick([deck](int port, bool on) { return DriveMotor(deck, port, on); });
}

} // namespace Frigate::Platform::Input
