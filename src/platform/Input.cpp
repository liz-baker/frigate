#include "platform/Input.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

#include <spdlog/spdlog.h>

#include "libultraship/bridge/controllerbridge.h"
#include "libultraship/libultra/motor.h"
#include "libultraship/libultra/os.h"
#include "platform/BindingsApply.h"
#include "platform/Rumble.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
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
    InitBindings(context);
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

AttachedDevices AttachedDevicesOf(Ship::ControlDeck* deck) {
    AttachedDevices attached;
    if (deck == nullptr) {
        return attached;
    }
    const auto manager = deck->GetConnectedPhysicalDeviceManager();
    for (const Ship::ConnectedGamepad& pad : manager->GetConnectedGamepads()) {
        attached.gamepads.push_back({ pad.id, pad.name });
    }
    attached.keyboardMouse = manager->HasKeyboard() && manager->HasMouse();
    return attached;
}

void ApplyRouting(Ship::ControlDeck* deck, const PortDevices& devices) {
    if (deck == nullptr) {
        return;
    }
    const AttachedDevices attached = AttachedDevicesOf(deck);
    const auto routes = Route(devices, attached);
    auto manager = deck->GetConnectedPhysicalDeviceManager();
    for (int port = 0; port < kPortCount; port++) {
        const PortRoute& route = routes[static_cast<size_t>(port)];
        const auto portIndex = static_cast<uint8_t>(port);
        for (const Gamepad& pad : attached.gamepads) {
            if (std::find(route.gamepads.begin(), route.gamepads.end(), pad.id) != route.gamepads.end()) {
                manager->UnignoreInstanceIdForPort(portIndex, pad.id);
            } else {
                manager->IgnoreInstanceIdForPort(portIndex, pad.id);
            }
        }
        const std::shared_ptr<Ship::Controller> controller = deck->GetControllerByPort(portIndex);
        if (controller == nullptr) {
            continue;
        }
        for (const Ship::PhysicalDeviceType type :
             { Ship::PhysicalDeviceType::Keyboard, Ship::PhysicalDeviceType::Mouse }) {
            // Clearing writes the whole config out once per mapping, so it is only done when there is
            // something to clear: routing runs again each time a device appears or goes, and on a Wii U
            // an unconditional clear on every port cost seconds on the SD card.
            if (route.keyboard == KeyboardRoute::Off) {
                if (controller->HasMappingsForPhysicalDeviceType(type)) {
                    controller->ClearAllMappingsForDeviceType(type);
                }
            } else if (route.keyboard == KeyboardRoute::On && !controller->HasMappingsForPhysicalDeviceType(type)) {
                controller->AddDefaultMappings(type);
            }
        }
        if (route.managed && !route.gamepads.empty() &&
            !controller->HasMappingsForPhysicalDeviceType(PHYSICAL_DEVICE_TYPE_GAMEPAD)) {
            controller->AddDefaultMappings(PHYSICAL_DEVICE_TYPE_GAMEPAD);
        }
    }
}

void SetDefaultBindings(Ship::ControlDeck* deck) {
    if (deck == nullptr) {
        return;
    }
    for (int port = 0; port < kPortCount; port++) {
        if (const auto controller = deck->GetControllerByPort(static_cast<uint8_t>(port))) {
            controller->ClearAllMappings();
        }
    }
    if (const auto first = deck->GetControllerByPort(0)) {
        first->AddDefaultMappings(Ship::PhysicalDeviceType::Keyboard);
        first->AddDefaultMappings(Ship::PhysicalDeviceType::Mouse);
        first->AddDefaultMappings(PHYSICAL_DEVICE_TYPE_GAMEPAD);
    }
    ApplyRouting(deck, Devices());
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

// TEMP DIAG: where Poll's time goes, logged once a second.
struct PollDiag {
    using Clock = std::chrono::steady_clock;
    double ms[12] = {};
    int frames = 0;
    int syncs = 0;
    int appliedRouting = 0;
    int appliedBindings = 0;
    int reasonDevices = 0;
    int reasonAttached = 0;
    int reasonBindings = 0;
    int reasonNotApplied = 0;
    double slowest = 0.0;
    Clock::time_point windowStart = Clock::now();
};
PollDiag gDiag;
double MsSince(PollDiag::Clock::time_point since) {
    return std::chrono::duration<double, std::milli>(PollDiag::Clock::now() - since).count();
}

// What the deck was last pointed at, so a change in Devices() or in what is
// attached (hot-plugging renumbers gamepads) is applied once.
struct AppliedRouting {
    bool applied = false;
    PortDevices devices{};
    AttachedDevices attached;
    uint32_t bindings = 0;
};

void SyncRouting(Ship::ControlDeck* deck) {
    static AppliedRouting last;
    static bool seen = false;
    if (deck == nullptr) {
        return;
    }
    const PortDevices& devices = Devices();
    const AttachedDevices attached = AttachedDevicesOf(deck);
    if (seen && last.attached != attached) {
        const std::string notice = HotplugNotice(last.attached, attached, devices);
        if (!notice.empty()) {
            DeviceNotice() = notice;
        }
    }
    seen = true;
    const bool unmanaged = std::all_of(devices.begin(), devices.end(),
                                       [](const PortDevice& device) { return device.kind == DeviceKind::Bindings; });
    const uint32_t bindings = Bindings().Generation();
    const bool routingChanged = last.devices != devices || last.attached != attached;
    if ((!routingChanged && last.bindings == bindings) || (unmanaged && !last.applied)) {
        last.attached = attached;
        return;
    }
    gDiag.syncs++;
    gDiag.reasonDevices += last.devices != devices;
    gDiag.reasonAttached += last.attached != attached;
    gDiag.reasonBindings += last.bindings != bindings;
    gDiag.reasonNotApplied += !last.applied;
    if (routingChanged || !last.applied) {
        const auto t = PollDiag::Clock::now();
        ApplyRouting(deck, devices);
        gDiag.appliedRouting++;
        gDiag.ms[6] += MsSince(t);
    }
    // After the routing, which may give a port default mappings this replaces.
    {
        const auto t = PollDiag::Clock::now();
        ApplyBindings(deck, Bindings(), devices);
        gDiag.appliedBindings++;
        gDiag.ms[7] += MsSince(t);
    }
    last.applied = true;
    last.devices = devices;
    last.attached = attached;
    last.bindings = bindings;
}

// What each port reads and whether it is in Modern mode, logged whenever any of it changes: the only
// record of why a port did or didn't end up on the device and mode the menu shows.
void LogPortState(Ship::ControlDeck* deck) {
    static bool logged = false;
    static PortDevices lastDevices;
    static AttachedDevices lastAttached;
    static std::array<BindingMode, kPortCount> lastModes{};
    const PortDevices& devices = Devices();
    const AttachedDevices attached = AttachedDevicesOf(deck);
    std::array<BindingMode, kPortCount> modes{};
    for (int port = 0; port < kPortCount; port++) {
        modes[static_cast<size_t>(port)] = Bindings().Mode(port);
    }
    if (logged && lastDevices == devices && lastAttached == attached && lastModes == modes) {
        return;
    }
    logged = true;
    lastDevices = devices;
    lastAttached = attached;
    lastModes = modes;

    std::string pads;
    for (const Gamepad& pad : attached.gamepads) {
        pads += (pads.empty() ? "" : ", ") + std::to_string(pad.id) + " " + pad.name;
    }
    spdlog::info("input: attached gamepads [{}], keyboard and mouse {}", pads, attached.keyboardMouse ? "yes" : "no");
    for (int port = 0; port < kPortCount; port++) {
        const auto index = static_cast<size_t>(port);
        const PortDevice& device = devices[index];
        const bool reads = device.kind == DeviceKind::KeyboardMouse || device.kind == DeviceKind::Gamepad;
        const bool attachedDevice = IsAttached(device, attached);
        const bool modern = reads && attachedDevice && modes[index] == BindingMode::Modern;
        spdlog::info("input: port {}: device '{}'{}, mode {}, {}", port + 1, DeviceKey(device),
                     attachedDevice ? "" : " (not attached)", modes[index] == BindingMode::Modern ? "Modern" : "N64",
                     modern            ? "Modern controls active"
                     : !reads          ? "Modern controls off: the port reads the default bindings or nothing"
                     : !attachedDevice ? "Modern controls off: the device is not attached"
                                       : "Modern controls off: the port's mode is N64");
    }
}

void PollModern(Ship::ControlDeck* deck, const OSContPad* pads, std::array<PadSample, kPortCount>& samples) {
    static std::array<ModernSynth, kPortCount> synths;
    static std::array<MenuPointerTracker, kPortCount> pointers;
    const PortDevices& devices = Devices();
    const AttachedDevices attached = AttachedDevicesOf(deck);
    bool captureMouse = false;
    bool pointerOnScreen = false;
    for (int port = 0; port < kPortCount; port++) {
        const auto index = static_cast<size_t>(port);
        const PortDevice& device = devices[index];
        const bool reads = device.kind == DeviceKind::KeyboardMouse || device.kind == DeviceKind::Gamepad;
        if (!reads || !IsAttached(device, attached) || Bindings().Mode(port) != BindingMode::Modern) {
            synths[index].Reset();
            pointers[index].Reset();
            ModernFrames()[index] = {};
            continue;
        }
        const ProfileDevice profileDevice =
            device.kind == DeviceKind::KeyboardMouse ? ProfileDevice::KeyboardMouse : ProfileDevice::Gamepad;
        const OSContPad& pad = pads[port];
        const DeviceFrame frame =
            ReadDeviceFrame(deck, device, { pad.stick_x, pad.stick_y, pad.right_stick_x, pad.right_stick_y });
        const ModernContext context = ModernContextOf(port);
        const ModernOutput output = synths[index].Step(Bindings().Get({ port, BindingMode::Modern, profileDevice }),
                                                       profileDevice, frame, context, ReadModernSettings(port));
        samples[index] = output.pad;
        // Modern-only departure: the mouse pointer drives the front-end menu cursor.
        if (device.kind == DeviceKind::KeyboardMouse && context.menu) {
            const bool navigating = output.pad.stickX != 0 || output.pad.stickY != 0;
            samples[index].pointer = pointers[index].Update(frame.pointer, navigating);
        } else {
            pointers[index].Reset();
        }
        ModernFrames()[index] = output.frame;
        ConnectedPorts() |= static_cast<uint8_t>(1 << port);
        pointerOnScreen = pointerOnScreen || (device.kind == DeviceKind::KeyboardMouse && frame.pointer.present);
        captureMouse = captureMouse || (device.kind == DeviceKind::KeyboardMouse && !context.menu);
    }
    SetModernMouseCapture(captureMouse);
    // The menu draws its own cursor, so the system pointer would be a second one.
    // A few frames' grace covers the frames the menu doesn't step.
    static int framesUnused = 0;
    framesUnused = TakeMenuPointerUsed() ? 0 : framesUnused + 1;
    HideSystemPointer(pointerOnScreen && framesUnused < 10);
}

} // namespace

void Poll(Ship::ControlDeck* deck) {
    const auto frameStart = PollDiag::Clock::now();
    auto lap = frameStart;
    const auto take = [&](int slot) {
        gDiag.ms[slot] += MsSince(lap);
        lap = PollDiag::Clock::now();
    };
    LoadBindings(deck);
    take(0);
    AutoPickDevice(deck);
    take(1);
    SyncRouting(deck);
    take(2);
    LogPortState(deck);
    take(3);
    OSContPad pads[kPortCount];
    ReadPads(deck, pads);
    take(4);
    std::array<PadSample, kPortCount> samples = SamplesFromPads(pads);
    const uint8_t detected = DetectedPortsOf(deck);
    ConnectedPorts() = EffectiveConnectedPorts(detected, ForcedPorts() | SessionForcedPorts());
    take(8);
    PollModern(deck, pads, samples);
    take(9);
    const auto& script = Script();
    if (script) {
        static uint32_t frame = 0;
        samples[0] = script->At(frame++);
        ModernFrames()[0] = {};
        ConnectedPorts() |= 1;
    }
    Advance(Current(), samples);
    take(10);

    // joyRumblePakTick.
    Rumble& rumble = GlobalRumble();
    rumble.SetReady(RumbleReadyPorts(detected, static_cast<bool>(script)));
    rumble.Tick([deck](int port, bool on) { return DriveMotor(deck, port, on); });
    take(11);

    gDiag.frames++;
    gDiag.slowest = std::max(gDiag.slowest, MsSince(frameStart));
    if (MsSince(gDiag.windowStart) >= 1000.0) {
        spdlog::info("input timing: {} polls, slowest {:.1f} ms; ms: load={:.1f} autopick={:.1f} sync={:.1f} "
                     "log={:.1f} readpads={:.1f} detected={:.1f} modern={:.1f} advance={:.1f} rumble={:.1f} "
                     "(applyRouting={:.1f} x{}, applyBindings={:.1f} x{}); syncs {} reapply reasons: devices={} "
                     "attached={} bindings={} notApplied={}; connected ports mask {:#x}",
                     gDiag.frames, gDiag.slowest, gDiag.ms[0], gDiag.ms[1], gDiag.ms[2], gDiag.ms[3], gDiag.ms[4],
                     gDiag.ms[8], gDiag.ms[9], gDiag.ms[10], gDiag.ms[11], gDiag.ms[6], gDiag.appliedRouting,
                     gDiag.ms[7], gDiag.appliedBindings, gDiag.syncs, gDiag.reasonDevices, gDiag.reasonAttached,
                     gDiag.reasonBindings, gDiag.reasonNotApplied, static_cast<unsigned>(ConnectedPorts()));
        gDiag = PollDiag{};
    }
}

} // namespace Frigate::Platform::Input
