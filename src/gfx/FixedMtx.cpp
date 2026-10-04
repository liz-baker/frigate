#include "gfx/FixedMtx.h"

namespace Frigate::Mtx {

FixedMtx ToFixedMtx(const float (&m)[4][4]) {
    return ToFixedMtx(m, 1.0f);
}

FixedMtx ToFixedMtx(const float (&m)[4][4], float scale) {
    FixedMtx out{};
    const float factor = 65536.0f * scale;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            auto a = static_cast<uint32_t>(static_cast<int32_t>(m[i][j] * factor));
            const float factorB = j + 1 == 3 ? 65536.0f : factor;
            auto b = static_cast<uint32_t>(static_cast<int32_t>(m[i][j + 1] * factorB));
            out.words[i * 2 + j / 2] = static_cast<int32_t>((a & 0xffff0000u) | (b >> 16));
            out.words[8 + i * 2 + j / 2] = static_cast<int32_t>((a << 16) | (b & 0xffffu));
        }
    }
    return out;
}

} // namespace Frigate::Mtx
