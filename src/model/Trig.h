#pragma once

#include <cmath>
#include <cstdint>

namespace Frigate {

// The game's sins() and coss(): a phase of 65536 to the turn, giving a signed
// 16-bit sine or cosine (32767 at the peak).
inline int16_t Sins(uint16_t phase) {
    return static_cast<int16_t>(std::lround(std::sin(phase * (2.0 * 3.14159265358979323846 / 65536.0)) * 32767.0));
}

inline int16_t Coss(uint16_t phase) {
    return static_cast<int16_t>(std::lround(std::cos(phase * (2.0 * 3.14159265358979323846 / 65536.0)) * 32767.0));
}

} // namespace Frigate
