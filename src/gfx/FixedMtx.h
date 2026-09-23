#pragma once

#include <cstdint>

namespace Frigate::Mtx {

// N64 fixed-point matrix: eight int-part words, then eight fraction-part
// words, as the F3D matrix handler reads them.
struct FixedMtx {
    int32_t words[16];
};

// `m` in the row-vector convention (translation in row 3).
FixedMtx ToFixedMtx(const float (&m)[4][4]);

} // namespace Frigate::Mtx
