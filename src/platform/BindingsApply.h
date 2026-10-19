#pragma once

#include <memory>
#include <vector>

#include "platform/BindingCapture.h"
#include "platform/Bindings.h"
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
// type become the profile's. Ports on Default bindings, ports in Modern mode
// (frigate#500 drives those) and a missing deck are left alone. Poll calls it
// when the store, the devices or the attached gamepads change.
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

} // namespace Frigate::Platform::Input
