#include "gfx/FixedMtx.h"

namespace Frigate::Mtx {

FixedMtx ToFixedMtx(const float (&m)[4][4]) {
    FixedMtx out{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            auto a = static_cast<uint32_t>(static_cast<int32_t>(m[i][j] * 65536.0f));
            auto b = static_cast<uint32_t>(static_cast<int32_t>(m[i][j + 1] * 65536.0f));
            out.words[i * 2 + j / 2] = static_cast<int32_t>((a & 0xffff0000u) | (b >> 16));
            out.words[8 + i * 2 + j / 2] = static_cast<int32_t>((a << 16) | (b & 0xffffu));
        }
    }
    return out;
}

} // namespace Frigate::Mtx
