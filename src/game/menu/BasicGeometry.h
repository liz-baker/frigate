#pragma once

#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

// gSPDisplayList(dlBasicGeometry): every geometry mode off but smooth
// shading, and texturing off.
inline Gfx* BasicGeometry(Gfx* dl) {
    using namespace Frigate::F3dex;
    *dl++ =
        ClearGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH | Fast::F3DEX_G_CULL_FRONT | Fast::F3DEX_G_CULL_BACK |
                          G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LOD);
    *dl++ = Texture(0, 0, 0, 0, 0);
    *dl++ = SetGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH);
    return dl;
}

// gSPDisplayList(dlZBufferGeometry): as BasicGeometry, with depth testing on.
inline Gfx* ZBufferGeometry(Gfx* dl) {
    dl = BasicGeometry(dl);
    *dl++ = F3dex::SetGeometryMode(G_ZBUFFER);
    return dl;
}

// gSPDisplayList(dlFastPipelineSetup): the RDP's 1-cycle, shade-only,
// opaque-surface baseline.
inline Gfx* FastPipelineSetup(Gfx* dl) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_PIPELINE, 1, G_PM_1PRIMITIVE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTDETAIL, 2, G_TD_CLAMP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    *dl++ = SetOtherModeH(G_MDSFT_COMBKEY, 1, G_CK_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_PASS | G_RM_OPA_SURF2);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_MAGICSQ);
    gDPPipeSync(dl++);
    return dl;
}

} // namespace Frigate::Game::Menu
