#include "game/menu/PropCamera.h"

#include <cmath>
#include <cstring>

#include "game/ScreenSize.h"
#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

Vp_t MenuViewport() {
    Vp_t viewport{};
    viewport.vscale[0] = (kMenuScreenWidth / 2) * 4;
    viewport.vscale[1] = (kMenuScreenHeight / 2) * 4;
    viewport.vscale[2] = 0x1ff;
    viewport.vscale[3] = 0;
    viewport.vtrans[0] = (kMenuScreenWidth / 2) * 4;
    viewport.vtrans[1] = (kMenuScreenHeight / 2) * 4;
    viewport.vtrans[2] = 0x1ff;
    viewport.vtrans[3] = 0;
    return viewport;
}

PropCamera BuildPropCamera(float distance, float aspect, float scale) {
    // FOV_Y_F (60 degrees) and viSetZRange(100, 10000)'s near/far -- the
    // same constants every front-end menu's interface function sets.
    constexpr float kFovY = 1.0471976f;
    constexpr float near = 100.0f;
    constexpr float far = 10000.0f;
    const float f = 1.0f / std::tan(kFovY / 2.0f);

    // Row-vector convention, as on the N64: translation lives in row 3.
    using Mat4Raw = float[4][4];
    Mat4Raw proj = {
        { f / aspect, 0, 0, 0 },
        { 0, f, 0, 0 },
        { 0, 0, (near + far) / (near - far), -1 },
        { 0, 0, 2 * near * far / (near - far), 0 },
    };

    // A straight lookat from (0,0,distance) toward the origin, up +Y -- no
    // yaw, unlike the viewer's orbit camera. `scale` multiplies the 3x3
    // block only (matrix_scalar_multiply() on the lookat's first three rows).
    Mat4Raw view = {
        { scale, 0, 0, 0 },
        { 0, scale, 0, 0 },
        { 0, 0, scale, 0 },
        { 0, 0, -distance, 1 },
    };

    PropCamera camera{};
    std::memcpy(camera.base.m, view, sizeof(view));
    camera.modelview = Mtx::ToFixedMtx(view);
    camera.projection = Mtx::ToFixedMtx(proj);

    camera.viewport = MenuViewport();
    return camera;
}

std::vector<Gfx> PropCameraPrefix(const PropCamera& camera) {
    using namespace Frigate::F3dex;
    std::vector<Gfx> prefix;
    prefix.push_back(Viewport(&camera.viewport));
    // Modelview first: the projection load multiplies by the top of the
    // modelview stack, which is empty until the first modelview load.
    prefix.push_back(Matrix(&camera.modelview, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD));
    prefix.push_back(Matrix(&camera.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD));
    return prefix;
}

} // namespace Frigate::Game::Menu
