// Temporary viewer glue, see GbiF3dex.h.
// TODO(https://github.com/liz-baker/frigate/issues/39): replaced by the
// orbit/zoom/pan controls and auto-framing.
#pragma once

#include <cstdint>

#include "libultraship/libultra/gbi.h"

namespace Frigate::Viewer {

// N64 fixed-point matrix: eight int-part words, then eight fraction-part
// words, as the F3D matrix handler reads them.
struct FixedMtx {
    int32_t words[16];
};

// `m` in the row-vector convention (translation in row 3).
FixedMtx ToFixedMtx(const float (&m)[4][4]);

struct Camera {
    FixedMtx projection;
    FixedMtx modelview;
    float view[4][4]; // the modelview as floats, the base of the model walk
    Vp_t viewport;
};

// Fixed camera looking down -Z at the origin from `distance`, yawed so the
// model isn't seen edge-on.
Camera MakeCamera(float distance, float aspect);

} // namespace Frigate::Viewer
