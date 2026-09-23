#pragma once

#include <vector>

#include "gfx/FixedMtx.h"
#include "libultraship/libultra/gbi.h"
#include "model/ModelWalk.h"

namespace Frigate::Game::Menu {

// A straight-on camera looking down -Z at the origin from `distance` --
// matches the front-end boot screens' own lookat framing, unlike the
// viewer's 30-degree orbit camera (Camera.h) built for asset inspection.
struct PropCamera {
    Mtx::FixedMtx modelview;
    Mtx::FixedMtx projection;
    Vp_t viewport;
    // The view matrix as floats -- BuildModelDraw's `base`, baked into every
    // node's matrix.
    Mat4 base;
};

// The viewport every front-end screen draws through: the full 440x330 menu
// framebuffer, 2 fractional bits.
Vp_t MenuViewport();

PropCamera BuildPropCamera(float distance, float aspect, float scale = 1.0f);

// Viewport + modelview/projection loads for `camera`. `camera` must already
// be at its final, stable storage location -- the returned commands point
// at its fields by address, matching src/model/ModelDraw.h's `prefix`
// contract.
std::vector<Gfx> PropCameraPrefix(const PropCamera& camera);

} // namespace Frigate::Game::Menu
