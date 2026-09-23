#pragma once

#include <cstdint>

// Shared with src/viewer/Camera.h (originally lived there alone); moved out
// once src/model/ModelDraw.h needed the same N64 fixed-point matrix format
// independent of the viewer's own camera code.
namespace Frigate::Mtx {

// N64 fixed-point matrix: eight int-part words, then eight fraction-part
// words, as the F3D matrix handler reads them.
struct FixedMtx {
    int32_t words[16];
};

// `m` in the row-vector convention (translation in row 3).
FixedMtx ToFixedMtx(const float (&m)[4][4]);

} // namespace Frigate::Mtx
