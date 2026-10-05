#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Frigate::Audio {

// The output level in percent, set from the menu's Settings tab and read by
// the audio thread.
inline std::atomic<int>& MasterVolumePercent() {
    static std::atomic<int> percent{ 100 };
    return percent;
}

inline void SetMasterVolume(int percent) {
    MasterVolumePercent() = std::clamp(percent, 0, 100);
}

// Scales `count` samples in place; 100 leaves them untouched.
inline void ApplyVolume(int16_t* samples, size_t count, int percent) {
    if (percent >= 100) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        samples[i] = static_cast<int16_t>(samples[i] * std::max(percent, 0) / 100);
    }
}

} // namespace Frigate::Audio
