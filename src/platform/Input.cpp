#include "platform/Input.h"

#include <cstring>

#include "libultraship/bridge/controllerbridge.h"
#include "libultraship/libultra/os.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/core/Context.h"

namespace Frigate::Platform::Input {

bool Init(const std::shared_ptr<Ship::Context>& context) {
    auto deck = context->GetChildren().GetFirst<Ship::ControlDeck>();
    if (deck == nullptr) {
        return false;
    }
    // osContInit finds the deck through this bridge cache.
    ControllerSetControlDeck(deck);
    // osContInit keeps the pointer to the connected-ports byte.
    OSContStatus status{};
    return osContInit(nullptr, ConnectedControllersStorage(), &status) == 0;
}

void ReadPads(Ship::ControlDeck* deck, OSContPad* pads) {
    std::memset(pads, 0, sizeof(OSContPad) * kPortCount);
    if (deck != nullptr) {
        deck->WriteToPad(pads);
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

void Poll(Ship::ControlDeck* deck) {
    OSContPad pads[kPortCount];
    ReadPads(deck, pads);
    Advance(Current(), SamplesFromPads(pads));
}

} // namespace Frigate::Platform::Input
