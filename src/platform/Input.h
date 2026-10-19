#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "libultraship/libultra/controller.h"
#include "platform/InputScript.h"
#include "platform/InputState.h"
#include "platform/PortAssignment.h"

namespace Ship {
class ControlDeck;
class Context;
} // namespace Ship

namespace Frigate::Platform::Input {

// Hands the context's ControlDeck to libultraship's osContInit, which loads
// the controller mappings and starts SDL's gamepad support. False when the
// context has no deck (it only exists with a window).
bool Init(const std::shared_ptr<Ship::Context>& context);

// Zeroes `pads` (kPortCount entries) and has `deck` fill it. ReadToPad ORs
// buttons into the buffer and only writes a stick that is still 0, so a stale
// buffer would keep last frame's input. A null deck leaves every port at zero.
void ReadPads(Ship::ControlDeck* deck, OSContPad* pads);

// The ports whose controller has an input mapped, as g_ConnectedControllers
// bits, read from the deck each call so a mapping added later counts. A port
// with nothing mapped is the unplugged one; it reads as a zeroed pad either
// way. A null deck connects no port.
uint8_t DetectedPortsOf(Ship::ControlDeck* deck);

// DetectedPortsOf plus the ports forced connected (ForcedPorts()); a forced
// port with nothing mapped reads idle input.
uint8_t ConnectedPortsOf(Ship::ControlDeck* deck);

// The gamepads attached, in the order libultraship lists them, and whether
// the combined Keyboard & Mouse device is available (both present; never on
// the Wii U). A null deck has nothing attached.
AttachedDevices AttachedDevicesOf(Ship::ControlDeck* deck);

// Points each port's gamepads, keyboard and mouse at what `devices` says,
// through the deck's per-port ignore lists and keyboard and mouse mappings. A
// port that gets a device it has no bindings for gets libultraship's default
// ones.
void ApplyRouting(Ship::ControlDeck* deck, const PortDevices& devices);

// Clears every port's mappings and gives port 1 libultraship's keyboard,
// mouse and gamepad defaults (what a first run starts with), then re-applies
// Devices() routing. A null deck does nothing.
void SetDefaultBindings(Ship::ControlDeck* deck);

// The pad buffer as per-port samples.
std::array<PadSample, kPortCount> SamplesFromPads(const OSContPad* pads);

// One frame's poll: ReadPads, then advance Current() and ConnectedPorts(). Call exactly once per
// displayed frame, before anything reads Current() or Game::Input. Re-applies
// Devices() when it or the attached gamepads changed. With a script set
// (InputScript.h) port 0 reads the script and counts as connected.
void Poll(Ship::ControlDeck* deck);

} // namespace Frigate::Platform::Input
