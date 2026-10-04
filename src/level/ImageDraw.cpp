#include "level/ImageDraw.h"

#include <cstdio>

#include "gfx/GbiF3dex.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

// texSetRenderMode for a single cycle without depth: the render mode a
// texSelect asks for.
Gfx* SetImageRenderMode(Gfx* dl, int32_t mode) {
    using namespace Frigate::F3dex;
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    switch (mode) {
        case 2:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
            break;
        case 3:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_TEX_EDGE | G_RM_AA_TEX_EDGE2);
            break;
        case 4:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_CLD_SURF | G_RM_CLD_SURF2);
            break;
        default:
            *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_OPA_SURF | G_RM_AA_OPA_SURF2);
            break;
    }
    return dl;
}

} // namespace

// texSelect for a plain (level 0) table entry: the render mode, the texture
// on, and the entry's load, which the packer wrote as the list
// `hud/img/<offset>`.
Gfx* SelectImage(Gfx* dl, int32_t offset, int32_t mode) {
    using namespace Frigate::F3dex;
    dl = SetImageRenderMode(dl, mode);
    *dl++ = Texture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, 1);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    gDPSetCombineMode(dl++, G_CC_MODULATEIA, G_CC_MODULATEIA);
    char path[32];
    std::snprintf(path, sizeof(path), "hud/img/%04x", static_cast<unsigned>(offset));
    return DisplayListByHash(dl, CRC64(path));
}

// draw_textured_rectangle: the rectangle centred on `position`, `size` being
// its half extents.
Gfx* DrawTexturedRectangle(Gfx* dl, const float position[2], const float size[2], int32_t width, int32_t height,
                           bool flipXY, bool flipX, bool flipY) {
    using namespace Frigate::F3dex;
    if (!(size[0] > 0.0f && size[1] > 0.0f)) {
        return dl;
    }
    int32_t s = 0;
    int32_t t = 0;

    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);

    int32_t xl = static_cast<int32_t>((position[0] - size[0]) * 4.0f);
    int32_t yl = static_cast<int32_t>((position[1] - size[1]) * 4.0f);
    const int32_t xh = static_cast<int32_t>((position[0] + size[0]) * 4.0f);
    const int32_t yh = static_cast<int32_t>((position[1] + size[1]) * 4.0f);

    if (xh >= 0 && yh >= 0) {
        if (xl < 0) {
            if (flipXY) {
                t += ((-xl * height) << 5) / (xh - xl);
            } else {
                s += ((-xl * width) << 5) / (xh - xl);
            }
            xl = 0;
        }
        if (yl < 0) {
            if (flipXY) {
                s += ((-yl * width) << 5) / (yh - yl);
            } else {
                t += ((-yl * height) << 5) / (yh - yl);
            }
            yl = 0;
        }

        int32_t dsdx;
        int32_t dtdy;
        if (flipXY) {
            dsdx = static_cast<int32_t>(static_cast<float>(width) / (2.0f * size[1]) * 1024.0f);
            dtdy = static_cast<int32_t>(static_cast<float>(height) / (2.0f * size[0]) * 1024.0f);
        } else {
            dsdx = static_cast<int32_t>(static_cast<float>(width) / (2.0f * size[0]) * 1024.0f);
            dtdy = static_cast<int32_t>(static_cast<float>(height) / (2.0f * size[1]) * 1024.0f);
        }
        if (flipX) {
            dsdx = 0x10000 - dsdx;
            s = ((width - 1) << 5) - s;
        }
        if (flipY) {
            dtdy = 0x10000 - dtdy;
            t = ((height - 1) << 5) - t;
        }

        if (flipXY) {
            gSPTextureRectangleFlip(dl++, xl, yl, xh, yh, G_TX_RENDERTILE, s, t, dsdx, dtdy);
        } else {
            gSPTextureRectangle(dl++, xl, yl, xh, yh, G_TX_RENDERTILE, s, t, dsdx, dtdy);
        }
    }

    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    return dl;
}

// display_image_at_position for a plain image with no highlight: the
// environment colour tints it, and its alpha is the environment's.
Gfx* DisplayImageAtPosition(Gfx* dl, const float position[2], const float size[2], int32_t width, int32_t height,
                            bool flipXY, bool flipX, bool flipY, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    if (!(size[0] > 0.0f && size[1] > 0.0f)) {
        return dl;
    }
    gDPSetEnvColor(dl++, r, g, b, alpha);
    gDPSetCombineLERP(dl++, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0,
                      ENVIRONMENT, 0);
    return DrawTexturedRectangle(dl, position, size, width, height, flipXY, flipX, flipY);
}

} // namespace Frigate::Level
