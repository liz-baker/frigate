#include "viewer/Camera.h"

#include <cmath>
#include <cstring>

namespace Frigate::Viewer {

using Mat4 = float[4][4];

Camera MakeCamera(float distance, float aspect) {
    constexpr float kFovY = 1.0471976f; // 60 degrees
    const float near = distance * 0.01f;
    const float far = distance * 10.0f;
    const float f = 1.0f / std::tan(kFovY / 2.0f);

    // Row-vector convention, as on the N64: translation lives in row 3.
    Mat4 proj = {
        { f / aspect, 0, 0, 0 },
        { 0, f, 0, 0 },
        { 0, 0, (near + far) / (near - far), -1 },
        { 0, 0, 2 * near * far / (near - far), 0 },
    };

    constexpr float kYaw = 0.5235988f; // 30 degrees
    const float c = std::cos(kYaw);
    const float s = std::sin(kYaw);
    Mat4 view = {
        { c, 0, -s, 0 },
        { 0, 1, 0, 0 },
        { s, 0, c, 0 },
        { 0, 0, -distance, 1 },
    };

    Camera camera{};
    std::memcpy(camera.view, view, sizeof(view));
    camera.projection = Mtx::ToFixedMtx(proj);
    camera.modelview = Mtx::ToFixedMtx(view);

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

} // namespace Frigate::Viewer
