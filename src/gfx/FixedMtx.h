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

// matrix_4x4_f32_to_s32 with the stage's visibility scale set by
// matrix_4x4_7F058C4C: every element but the fourth column is multiplied by
// `scale` on the way in, so the matrix shrinks the coordinates it transforms.
FixedMtx ToFixedMtx(const float (&m)[4][4], float scale);

} // namespace Frigate::Mtx
