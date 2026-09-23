#include "game/menu/PropCamera.h"

#include <cmath>
#include <cstring>

#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

PropCamera BuildPropCamera(float distance, float aspect) {
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
    // yaw, unlike the viewer's orbit camera.
    Mat4Raw view = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, -distance, 1 },
    };

    PropCamera camera{};
    std::memcpy(camera.base.m, view, sizeof(view));
    camera.modelview = Mtx::ToFixedMtx(view);
    camera.projection = Mtx::ToFixedMtx(proj);

    // 2 fractional bits, centered on the 320x240 framebuffer.
    camera.viewport.vscale[0] = 160 * 4;
    camera.viewport.vscale[1] = 120 * 4;
    camera.viewport.vscale[2] = 0x1ff;
    camera.viewport.vscale[3] = 0;
    camera.viewport.vtrans[0] = 160 * 4;
    camera.viewport.vtrans[1] = 120 * 4;
    camera.viewport.vtrans[2] = 0x1ff;
    camera.viewport.vtrans[3] = 0;
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
