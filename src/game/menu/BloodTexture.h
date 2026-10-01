#pragma once

#include "game/menu/BloodAnimation.h"
#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

// gDPLoadTextureBlock_4b of a blood frame, as the gun barrel and the death
// overlay draw it: loaded as 16-bit texels, drawn as 4-bit intensity. The
// SETTIMG's width is the real row width in 16-bit texels.
inline Gfx* LoadBloodTexture(Gfx* dl, const BloodAnimation& blood) {
    using namespace Frigate::F3dex;
    constexpr int kRowWords = kBloodTextureWidth / 4;
    gDPSetTextureImage(dl++, G_IM_FMT_I, G_IM_SIZ_16b, kRowWords, const_cast<uint8_t*>(blood.Texture()));
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(dl++);
    constexpr int kLine = (kRowWords + 3) / 4;
    gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, ((kBloodTextureWidth * kBloodTextureHeight + 3) >> 2) - 1,
                 (2048 + kLine - 1) / kLine);
    gDPPipeSync(dl++);
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_4b, ((kBloodTextureWidth >> 1) + 7) >> 3, 0, G_TX_RENDERTILE, 0,
               G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD);
    gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (kBloodTextureWidth - 1) << G_TEXTURE_IMAGE_FRAC,
                   (kBloodTextureHeight - 1) << G_TEXTURE_IMAGE_FRAC);
    return dl;
}

} // namespace Frigate::Game::Menu
