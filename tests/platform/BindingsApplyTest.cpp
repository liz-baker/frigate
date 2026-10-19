#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "libultraship/libultra/controller.h"
#include "platform/BindingsApply.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldeck/ControlPort.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToAxisDirectionMapping.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/mouse/MouseWheelToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h"
#include "ship/controller/controldevice/controller/mapping/sdl/SDLButtonToButtonMapping.h"

using namespace Frigate::Platform::Input;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

class FakeDeck : public Ship::ControlDeck {
  public:
    FakeDeck() : Ship::ControlDeck({}, nullptr, {}, nullptr, std::make_shared<Ship::ConsoleVariable>()) {
    }
    void WriteToPad(void*) override {
    }
};

class PadController : public Ship::Controller {
  public:
    PadController(uint8_t port, std::shared_ptr<Ship::ConsoleVariable> cvars)
        : Ship::Controller(port,
                           { BTN_A, BTN_B, BTN_START, BTN_L, BTN_R, BTN_Z, BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT,
                             BTN_CUP, BTN_CDOWN, BTN_CLEFT, BTN_CRIGHT },
                           std::move(cvars), nullptr, nullptr) {
    }
    void ReadToPad(void*) override {
    }
};

size_t ButtonMappings(const std::shared_ptr<Ship::Controller>& controller, CONTROLLERBUTTONS_T bitmask) {
    return controller->GetButton(bitmask)->GetAllButtonMappings().size();
}

bool Contains(const std::vector<LegacyBinding>& legacy, Action action, const PhysicalInput& input) {
    for (const LegacyBinding& binding : legacy) {
        if (binding.action == action && binding.input == input) {
            return true;
        }
    }
    return false;
}

// A deck with one controller on port 1.
class OnePortDeck : public FakeDeck {
  public:
    explicit OnePortDeck(const std::shared_ptr<PadController>& controller) {
        mPorts.push_back(std::make_shared<Ship::ControlPort>(0, controller));
    }
};

} // namespace

int main() {
    auto cvars = std::make_shared<Ship::ConsoleVariable>();
    auto controller = std::make_shared<PadController>(0, cvars);
    auto deck = std::make_shared<OnePortDeck>(controller);
    controller->SetControlDeck(deck);
    InitBindings(deck, nullptr, cvars);

    // Migration: the mappings libultraship loaded become the N64 profiles.
    controller->GetButton(BTN_A)->AddButtonMapping(
        std::make_shared<Ship::KeyboardKeyToButtonMapping>(0, BTN_A, Ship::LUS_KB_G, deck, nullptr, cvars));
    controller->GetButton(BTN_A)->GetAllButtonMappings().begin()->second->SaveToConfig();
    controller->GetButton(BTN_Z)->AddButtonMapping(
        std::make_shared<Ship::MouseWheelToButtonMapping>(0, BTN_Z, Ship::LUS_WHEEL_UP, deck, cvars));
    controller->GetButton(BTN_Z)->GetAllButtonMappings().begin()->second->SaveToConfig();
    controller->GetButton(BTN_B)->AddButtonMapping(
        std::make_shared<Ship::SDLButtonToButtonMapping>(0, BTN_B, SDL_GAMEPAD_BUTTON_NORTH, deck, cvars));
    controller->GetButton(BTN_B)->GetAllButtonMappings().begin()->second->SaveToConfig();
    controller->GetButton(BTN_CUP)->AddButtonMapping(
        std::make_shared<Ship::SDLAxisDirectionToButtonMapping>(0, BTN_CUP, SDL_GAMEPAD_AXIS_RIGHTY, -1, deck, cvars));
    controller->GetButton(BTN_CUP)->GetAllButtonMappings().begin()->second->SaveToConfig();
    controller->GetLeftStick()->AddAxisDirectionMapping(
        Ship::UP, std::make_shared<Ship::KeyboardKeyToAxisDirectionMapping>(0, Ship::LEFT_STICK, Ship::UP,
                                                                            Ship::LUS_KB_I, deck, nullptr, cvars));
    controller->GetLeftStick()->GetAllAxisDirectionMappingByDirection(Ship::UP).begin()->second->SaveToConfig();

    const std::vector<LegacyBinding> legacy = ReadLegacyBindings(deck.get(), 0);
    Check(Contains(legacy, Action::N64A, KeyInput(Ship::LUS_KB_G)), "a keyboard button mapping is read back");
    Check(Contains(legacy, Action::N64Z, WheelInput(WheelDir::Up)), "and a wheel one");
    Check(Contains(legacy, Action::N64B, PadInput(PadButton::North)), "and a gamepad button by position");
    Check(Contains(legacy, Action::N64CUp, AxisInput(PadAxis::RightY, 1)),
          "SDL's up (negative Y) reads as the store's up");
    Check(Contains(legacy, Action::N64StickUp, KeyInput(Ship::LUS_KB_I)), "and a stick direction");
    Check(ReadLegacyBindings(deck.get(), 1).empty(), "a port without a controller has none");

    LoadBindings(deck.get());
    const BindTarget kbm{ 0, BindingMode::N64, ProfileDevice::KeyboardMouse };
    const BindTarget pad{ 0, BindingMode::N64, ProfileDevice::Gamepad };
    Check(Bindings().Get(kbm).Of(Action::N64A).size() == 1 &&
              Bindings().Get(kbm).Of(Action::N64A)[0] == KeyInput(Ship::LUS_KB_G),
          "first launch migrates the keyboard profile");
    Check(Bindings().Get(kbm).Of(Action::N64B) ==
              DefaultProfile(BindingMode::N64, ProfileDevice::KeyboardMouse).Of(Action::N64B),
          "a required action with no keyboard mapping gets its default");
    Check(Bindings().Get(pad).Of(Action::N64B).size() == 1 &&
              Bindings().Get(pad).Of(Action::N64B)[0] == PadInput(PadButton::North),
          "and the gamepad profile");
    Check(Bindings().Get({ 1, BindingMode::N64, ProfileDevice::Gamepad }) ==
              DefaultProfile(BindingMode::N64, ProfileDevice::Gamepad),
          "a port without mappings keeps the defaults");
    Check(cvars->GetInteger("gBindingsMigrated", 0) == 1 &&
              std::string(cvars->GetString("gPort1Bindings", "")) == Bindings().Serialize(0),
          "the migration is saved, with the flag");

    // Applying: the port's keyboard mappings become the store's, a gamepad
    // port's mappings are left alone, and Default bindings are not touched.
    Bindings().ResetAll();
    Bindings().Bind(kbm, Action::N64A, KeyInput(Ship::LUS_KB_V), 0);
    PortDevices devices{};
    ApplyBindings(deck.get(), Bindings(), devices);
    Check(Contains(ReadLegacyBindings(deck.get(), 0), Action::N64A, KeyInput(Ship::LUS_KB_G)),
          "a port on Default bindings keeps libultraship's mappings");

    devices[0] = { DeviceKind::KeyboardMouse, {}, 0 };
    ApplyBindings(deck.get(), Bindings(), devices);
    const std::vector<LegacyBinding> applied = ReadLegacyBindings(deck.get(), 0);
    Check(Contains(applied, Action::N64A, KeyInput(Ship::LUS_KB_V)) &&
              !Contains(applied, Action::N64A, KeyInput(Ship::LUS_KB_G)),
          "a keyboard port's A mapping is the store's");
    Check(Contains(applied, Action::N64StickUp, KeyInput(Ship::LUS_KB_W)) &&
              Contains(applied, Action::N64CUp, KeyInput(Ship::LUS_KB_ARROWKEY_UP)),
          "with the stick and C buttons");
    Check(Contains(applied, Action::N64B, PadInput(PadButton::North)),
          "and the gamepad mappings the port had are not cleared");
    Check(ButtonMappings(controller, BTN_Z) == 1, "Z has the one keyboard binding");

    Bindings().SetMode(0, BindingMode::Modern);
    Bindings().Bind(kbm, Action::N64A, KeyInput(Ship::LUS_KB_J), 0);
    ApplyBindings(deck.get(), Bindings(), devices);
    Check(Contains(ReadLegacyBindings(deck.get(), 0), Action::N64A, KeyInput(Ship::LUS_KB_V)),
          "a port in Modern mode keeps its mappings");

    return gFailures == 0 ? 0 : 1;
}
