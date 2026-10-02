#pragma once

#include <array>
#include <memory>

#include "libultraship/libultra/controller.h"
#include "platform/InputState.h"

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
// bits. libultraship keeps four always-present controllers and no
// plugged-in state of its own, so a port with nothing mapped is the unplugged
// one; it reads as a zeroed pad either way. A null deck connects no port.
uint8_t ConnectedPortsOf(Ship::ControlDeck* deck);

// The pad buffer as per-port samples.
std::array<PadSample, kPortCount> SamplesFromPads(const OSContPad* pads);

// One frame's poll: ReadPads, then advance Current() and ConnectedPorts(). Call exactly once per
// displayed frame, before anything reads Current() or Game::Input.
void Poll(Ship::ControlDeck* deck);

} // namespace Frigate::Platform::Input
