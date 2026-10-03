#include "platform/Input.h"

#include <cstdio>
#include <cstring>

#include "libultraship/libultra/controller.h"
#include "libultraship/libultra/os.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldeck/ControlPort.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToButtonMapping.h"

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// Fills the pad buffer the way Controller::ReadToPad does: buttons are ORed
// in and a stick is only written while it is still 0.
class FakeDeck : public Ship::ControlDeck {
  public:
    FakeDeck() : Ship::ControlDeck({}, nullptr, {}, nullptr, std::make_shared<Ship::ConsoleVariable>()) {
    }

    void WriteToPad(void* pads) override {
        auto* pad = static_cast<OSContPad*>(pads);
        for (int port = 0; port < kPortCount; port++) {
            pad[port].button |= buttons[port];
            if (pad[port].stick_x == 0) {
                pad[port].stick_x = stickX[port];
            }
            if (pad[port].stick_y == 0) {
                pad[port].stick_y = stickY[port];
            }
        }
        writes++;
    }

    uint16_t buttons[kPortCount] = {};
    int8_t stickX[kPortCount] = {};
    int8_t stickY[kPortCount] = {};
    int writes = 0;
};

// A controller with no output of its own, to hold mappings.
class MappedController : public Ship::Controller {
  public:
    MappedController(uint8_t port, std::shared_ptr<Ship::ConsoleVariable> consoleVariable)
        : Ship::Controller(port, { BTN_A }, std::move(consoleVariable), nullptr, nullptr) {
    }

    void ReadToPad(void*) override {
    }
};

// A deck of four ports, each holding a controller with a key mapped to A when
// `mapped` has its bit set.
class PortedDeck : public FakeDeck {
  public:
    explicit PortedDeck(uint8_t mapped) {
        auto consoleVariable = std::make_shared<Ship::ConsoleVariable>();
        for (int port = 0; port < kPortCount; port++) {
            auto controller = std::make_shared<MappedController>(static_cast<uint8_t>(port), consoleVariable);
            if ((mapped & (1 << port)) != 0) {
                controller->GetButton(BTN_A)->AddButtonMapping(std::make_shared<Ship::KeyboardKeyToButtonMapping>(
                    static_cast<uint8_t>(port), BTN_A, Ship::LUS_KB_A));
            }
            mPorts.push_back(std::make_shared<Ship::ControlPort>(static_cast<uint8_t>(port), controller));
        }
    }
};

} // namespace

int main() {
    // Stale buffer contents never survive a read: last frame's buttons and
    // stick would otherwise be ORed or kept.
    {
        FakeDeck deck;
        deck.buttons[0] = BTN_A;
        deck.stickX[0] = 10;
        OSContPad pads[kPortCount];
        std::memset(pads, 0x5A, sizeof(pads));
        ReadPads(&deck, pads);
        Check(pads[0].button == BTN_A, "stale buttons are cleared");
        Check(pads[0].stick_x == 10, "a stale stick doesn't mask the new one");
        Check(pads[1].button == 0 && pads[3].stick_y == 0 && pads[2].err_no == 0, "other ports are zeroed");
    }

    // A missing deck (no window) reads as no input.
    {
        OSContPad pads[kPortCount];
        std::memset(pads, 0xFF, sizeof(pads));
        ReadPads(nullptr, pads);
        bool allZero = true;
        for (int port = 0; port < kPortCount; port++) {
            allZero = allZero && pads[port].button == 0 && pads[port].stick_x == 0 && pads[port].stick_y == 0;
        }
        Check(allZero, "null deck zeroes every port");
    }

    // OSContPad masks match the decomp's, so the buttons pass through as-is.
    {
        OSContPad pads[kPortCount] = {};
        pads[0].button = BTN_START | BTN_Z;
        pads[0].stick_x = -70;
        pads[0].stick_y = 55;
        pads[3].button = BTN_CLEFT;
        const auto samples = SamplesFromPads(pads);
        Check(samples[0].buttons == 0x3000, "Start and Z map to the N64 masks");
        Check(samples[0].stickX == -70 && samples[0].stickY == 55, "stick is copied");
        Check(samples[3].buttons == 0x0002, "port 4 is converted");
        Check(samples[1].buttons == 0, "idle port is zero");
    }

    // Polling: a held button reports pressed once, and releasing then
    // pressing again reports it again. A null deck keeps Poll safe.
    {
        Current() = State{};
        FakeDeck deck;
        deck.buttons[0] = BTN_A;
        Poll(&deck);
        Check(Current()[0].pressed == BTN_A, "first poll reports the press");
        Poll(&deck);
        Check(Current()[0].pressed == 0 && Current()[0].buttons == BTN_A, "a held button is not pressed again");
        deck.buttons[0] = 0;
        Poll(&deck);
        Check(Current()[0].buttons == 0, "released");
        Poll(nullptr);
        Check(Current()[0].pressed == 0, "polling without a deck reads zero");
        Check(deck.writes == 3, "one WriteToPad per poll");
    }

    // A port counts as connected when its controller has an input mapped.
    {
        Check(ConnectedPortsOf(nullptr) == 0, "no deck connects no port");
        FakeDeck fakeNoPorts;
        Check(ConnectedPortsOf(&fakeNoPorts) == 0, "a deck with no ports connects none");
        PortedDeck none(0x0);
        Check(ConnectedPortsOf(&none) == 0, "unmapped controllers are unplugged ports");
        PortedDeck two(0x3);
        Check(ConnectedPortsOf(&two) == 0x3, "mapped ports are connected");
        Check(ControllerCount(ConnectedPortsOf(&two)) == 2, "and counted");
        PortedDeck gap(0xD);
        Check(ConnectedPortsOf(&gap) == 0xD, "each port is judged on its own");
        Check(ControllerCount(ConnectedPortsOf(&gap)) == 1, "though the count stops at the gap");

        Current() = State{};
        Poll(&two);
        Check(ConnectedPorts() == 0x3, "polling records the connected ports");
        Poll(nullptr);
        Check(ConnectedPorts() == 0, "and clears them without a deck");
    }

    return gFailures == 0 ? 0 : 1;
}
