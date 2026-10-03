#include "game/menu/FolderBackground.h"

#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

namespace {

constexpr int kWidth = 440;
constexpr int kHeight = 299;
constexpr int kTop = 0x10;

// gDPLoadTextureBlock() of one row of the 8-bit backdrop, clamped in both
// axes. The SETTIMG carries the image's real row width (see
// src/game/TextRender.cpp).
Gfx* LoadRow(Gfx* dl, const uint8_t* row) {
    gDPSetTextureImage(dl++, G_IM_FMT_I, G_IM_SIZ_8b, kWidth, const_cast<uint8_t*>(row));
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(dl++);
    constexpr int kLine = (kWidth + 7) / 8;
    gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, kWidth - 1, (2048 + kLine - 1) / kLine);
    gDPPipeSync(dl++);
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, kLine, 0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (kWidth - 1) << G_TEXTURE_IMAGE_FRAC, 0);
    return dl;
}

uint8_t Blend(int32_t top, int32_t bottom, int row) {
    const auto start = static_cast<float>(top);
    return static_cast<uint8_t>(
        static_cast<int>(start + ((static_cast<float>(bottom) - start) * static_cast<float>(row)) / 299.0f));
}

} // namespace

Gfx* DrawFolderBackground(Gfx* dl, const Fast::Texture& backdrop, int32_t xOffset, const FolderColour& top,
                          const FolderColour& bottom) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_OPA_SURF | G_RM_OPA_SURF2);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_POINT);
    gDPPipeSync(dl++);

    const uint8_t* image = backdrop.ImageData;
    for (int i = 0; i < kHeight; i++) {
        dl = LoadRow(dl, image + i * kWidth);
        gDPSetPrimColor(dl++, 0, 0, Blend(top.red, bottom.red, i), Blend(top.green, bottom.green, i),
                        Blend(top.blue, bottom.blue, i), 255);

        // The decomp ends each rectangle a quarter pixel short of the next
        // row's top, which the RDP rasterises as the same one pixel row. The
        // interpreter draws the fraction, leaving seams once the frame is
        // scaled, so the rectangles end on the pixel edge.
        const int32_t rowTop = (i + kTop) << 2;
        const int32_t rowBottom = (i + 1 + kTop) << 2;
        if (xOffset < 0) {
            gSPTextureRectangle(dl++, 0, rowTop, kWidth << 2, rowBottom, G_TX_RENDERTILE, (-xOffset) << 5, 0, 1 << 10,
                                1 << 10);
        } else {
            gSPTextureRectangle(dl++, xOffset << 2, rowTop, kWidth << 2, rowBottom, G_TX_RENDERTILE, 0, 0, 1 << 10,
                                1 << 10);
        }
    }
    return dl;
}

} // namespace Frigate::Game::Menu
