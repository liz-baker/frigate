#pragma once

#include <memory>
#include <vector>

#include "platform/BindingCapture.h"
#include "platform/Bindings.h"
#include "platform/ModernInput.h"
#include "platform/PortAssignment.h"

namespace Ship {
class ConsoleVariable;
class ControlDeck;
class Context;
class Window;
} // namespace Ship

// The libultraship side of the binding store (Bindings.h): loads and saves it
// in the config, drives libultraship's per-port mappings from it, and reads
// the inputs a capture listens to. Everything here goes through libultraship's
// mapping classes and PHYSICAL_DEVICE_TYPE_GAMEPAD, so it covers the Wii U's
// native pads the same way as SDL gamepads. Model: docs/bindings.md.
namespace Frigate::Platform::Input {

// Gives the functions below the deck, window and config they need. Input.h's
// Init calls it.
void InitBindings(const std::shared_ptr<Ship::Context>& context);
void InitBindings(std::shared_ptr<Ship::ControlDeck> deck, std::shared_ptr<Ship::Window> window,
                  std::shared_ptr<Ship::ConsoleVariable> cvars);

// Fills Bindings() from the config on the first call. The first launch with
// the store (none saved yet) migrates the mappings libultraship loaded from
// the config instead. Needs the deck's mappings loaded; Poll calls it.
void LoadBindings(Ship::ControlDeck* deck);

// Writes Bindings() to the config. Call after editing the store.
void SaveBindings();

// Makes each port with a device of its own (Keyboard & Mouse or a gamepad)
// use that device's N64 profile: its libultraship mappings for that device
// type become the profile's. A port in Modern mode loses the device's mappings
// (ModernSynth reads the device itself) except a gamepad's two sticks. Ports on
// Default bindings and a missing deck are left alone. Poll calls it when the
// store, the devices or the attached gamepads change.
void ApplyBindings(Ship::ControlDeck* deck, const BindingStore& store, const PortDevices& devices);

// Every N64 button and stick mapping libultraship has for `port`, as bindings
// for ProfileFromLegacy.
std::vector<LegacyBinding> ReadLegacyBindings(Ship::ControlDeck* deck, int port);

// How the port's selected gamepad prints its face buttons; Xbox when the port
// has no gamepad. The Wii U's are always Nintendo.
FaceLayout FaceLayoutOf(Ship::ControlDeck* deck, const PortDevice& device);

// Every input held right now on every attached device, for Capture::Update.
// Keyboard and mouse come from ImGui's state, so this is for use while the
// menu is open. `mouseOverUi` leaves out the mouse buttons (the editor passes
// true while the pointer is over its own buttons, so pressing Cancel can't
// bind the click). Mouse motion counts once it moves past a few pixels a frame.
std::vector<RawInput> PollRawInputs(Ship::ControlDeck* deck, bool mouseOverUi);

// The left and right stick values libultraship made of a gamepad port's stick
// mappings (the port's dead zone, sensitivity and notch settings applied), in
// the pad's units.
struct PadSticks {
    int8_t leftX = 0;
    int8_t leftY = 0;
    int8_t rightX = 0;
    int8_t rightY = 0;
};

// One frame of what a Modern port reads, for ModernSynth. A keyboard and mouse
// reads ImGui's key and button state (blocked while the menu has the keyboard
// or the pointer), the wheel the deck read this frame and, when the mouse is
// captured, its motion; a gamepad reads its buttons and triggers directly and
// `sticks`. The mouse motion is consumed from the window, so call once per
// frame, after the deck's pads were read.
DeviceFrame ReadDeviceFrame(Ship::ControlDeck* deck, const PortDevice& device, const PadSticks& sticks);

// The port's Modern settings from the config (the sensitivities are percent
// there).
ModernSettings ReadModernSettings(int port);

// Whether the mouse should be captured: wanted while a Modern keyboard and
// mouse port is in live play. The ImGui menu releases it while open.
void SetModernMouseCapture(bool wanted);

// Hides the operating system's pointer over the game window, or gives it back
// to libultraship's own show-on-move and idle timeout.
void HideSystemPointer(bool hide);

} // namespace Frigate::Platform::Input
