#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "platform/InputState.h"

namespace Frigate::Platform::Input {

// The rumble pak state of joy.c (g_ContRumblePak*), with no libultraship
// dependency; Poll() (platform/Input.h) ticks it against the controllers'
// rumble motors.
class Rumble {
  public:
    // osMotorStart (true) and osMotorStop (false) for a port; true when the
    // motor answered.
    using Motor = std::function<bool(int port, bool on)>;

    // The controllers that can rumble: g_ContRumblePakInitState above
    // RUMBLEPAKINITSTATE_NOT_READY. A motor that fails to answer takes its
    // port out until this is set again.
    void SetReady(uint8_t ports) {
        ready_ = ports;
    }

    // joyRumblePakStart: runs the motor of `port` for at least `duration`
    // seconds.
    void Start(int port, float duration);

    // joyRumblePakTick: one 60th of a second. Starts and stops the motors and
    // counts the running ones down.
    void Tick(const Motor& motor);

    // g_ContRumblePakTimer60.
    int Timer(int port) const {
        return timer_[static_cast<size_t>(port)];
    }
    bool Running(int port) const {
        return current_[static_cast<size_t>(port)] == State::On;
    }

  private:
    enum class State { Off, On };

    std::array<int, kPortCount> timer_{};
    std::array<State, kPortCount> current_{};
    std::array<State, kPortCount> target_{};
    uint8_t ready_ = 0;
};

// The ports whose controllers can be told to rumble: the ones a device was
// found for, and port 1 under a scripted pad. A port that is only forced
// connected (the setting, or a quick-started game's players) has no motor.
constexpr uint8_t RumbleReadyPorts(uint8_t detected, bool scripted) {
    return static_cast<uint8_t>(detected | (scripted ? 1 : 0));
}

// The rumble the game drives.
Rumble& GlobalRumble();

} // namespace Frigate::Platform::Input
