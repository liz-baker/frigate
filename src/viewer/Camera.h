// Temporary viewer glue, see GbiF3dex.h.
// TODO(https://github.com/liz-baker/frigate/issues/39): replaced by the
// orbit/zoom/pan controls and auto-framing.
#pragma once

#include <cstdint>

#include "gfx/FixedMtx.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Viewer {

struct Camera {
    Mtx::FixedMtx projection;
    Mtx::FixedMtx modelview;
    float view[4][4]; // the modelview as floats, the base of the model walk
    Vp_t viewport;
};

// Fixed camera looking down -Z at the origin from `distance`, yawed so the
// model isn't seen edge-on.
Camera MakeCamera(float distance, float aspect);

} // namespace Frigate::Viewer
