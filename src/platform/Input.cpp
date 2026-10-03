#include "platform/Input.h"

#include <cstring>

#include "libultraship/bridge/controllerbridge.h"
#include "libultraship/libultra/os.h"
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

uint8_t ConnectedPortsOf(Ship::ControlDeck* deck) {
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

std::array<PadSample, kPortCount> SamplesFromPads(const OSContPad* pads) {
    std::array<PadSample, kPortCount> samples;
    for (int port = 0; port < kPortCount; port++) {
        samples[port].stickX = pads[port].stick_x;
        samples[port].stickY = pads[port].stick_y;
        samples[port].buttons = static_cast<uint16_t>(pads[port].button);
    }
    return samples;
}

void Poll(Ship::ControlDeck* deck) {
    OSContPad pads[kPortCount];
    ReadPads(deck, pads);
    std::array<PadSample, kPortCount> samples = SamplesFromPads(pads);
    ConnectedPorts() = ConnectedPortsOf(deck);
    if (const auto& script = Script()) {
        static uint32_t frame = 0;
        samples[0] = script->At(frame++);
        ConnectedPorts() |= 1;
    }
    Advance(Current(), samples);
}

} // namespace Frigate::Platform::Input
