#include "platform/Rumble.h"

namespace Frigate::Platform::Input {

void Rumble::Start(int port, float duration) {
    if (port < 0 || port >= kPortCount || (ready_ & (1u << port)) == 0) {
        return;
    }
    const auto index = static_cast<size_t>(port);
    const int duration60 = static_cast<int>(duration * 60.0f);
    if (timer_[index] < duration60) {
        timer_[index] = duration60;
    }
    if (current_[index] == State::Off) {
        target_[index] = State::On;
    }
}

void Rumble::Tick(const Motor& motor) {
    for (int port = 0; port < kPortCount; port++) {
        const auto index = static_cast<size_t>(port);
        if (current_[index] != target_[index]) {
            const bool on = target_[index] == State::On;
            if (motor && motor(port, on)) {
                current_[index] = target_[index];
            } else {
                ready_ &= static_cast<uint8_t>(~(1u << port));
            }
        }
        if (timer_[index] <= 0) {
            timer_[index] = 0;
        } else {
            timer_[index]--;
            if (timer_[index] <= 0) {
                timer_[index] = 0;
                target_[index] = State::Off;
            }
        }
    }
}

Rumble& GlobalRumble() {
    static Rumble rumble;
    return rumble;
}

} // namespace Frigate::Platform::Input
