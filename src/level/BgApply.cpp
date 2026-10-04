#include "level/BgApply.h"

#include <vector>

#include "gfx/GbiF3dex.h"

// Each RDP command is built into a temporary Gfx by the libultra macro, which
// takes the command pointer as its first argument. The macros that expand to
// the RSP's set-other-mode command aren't used: they number it as F3DEX2 does,
// and the indy ucode numbers it as F3DEX (gfx/GbiF3dex.h).
#define PUSH(out, command)     \
    do {                       \
        Gfx cmd_{};            \
        Gfx* gfx_ = &cmd_;     \
        command;               \
        (out).push_back(cmd_); \
    } while (0)

namespace Frigate::Level {

namespace {

// gDPSetRenderMode().
void RenderMode(std::vector<Gfx>& out, uint32_t cycle1, uint32_t cycle2) {
    out.push_back(F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, cycle1 | cycle2));
}

std::vector<Gfx> PrimaryAddFog() {
    std::vector<Gfx> out;
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_OPA_TERR2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_TERR2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_OPA_DECAL2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_DECAL2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_DECAL2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_XLU_SURF2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
    RenderMode(out, G_RM_PASS, G_RM_AA_OPA_SURF2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_OPA_SURF2);
    RenderMode(out, G_RM_PASS, G_RM_AA_OPA_TERR2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_OPA_TERR2);
    return out;
}

std::vector<Gfx> SecondaryAddFog() {
    std::vector<Gfx> out;
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_DECAL2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_XLU_SURF2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
    RenderMode(out, G_RM_PASS, G_RM_AA_ZB_TEX_EDGE2);
    RenderMode(out, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0,
                                ENVIRONMENT, 0));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEI2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0,
                                ENVIRONMENT, 0));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEI2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, TEXEL1, 0, COMBINED, 0, SHADE, 0,
                                COMBINED, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, TEXEL1, 0, COMBINED, 0, SHADE, 0,
                                COMBINED, 0, ENVIRONMENT, 0));
    return out;
}

std::vector<Gfx> Primary() {
    std::vector<Gfx> out;
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0,
                                ENVIRONMENT, 0));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEI2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0,
                                ENVIRONMENT, 0));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEI2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT));
    return out;
}

std::vector<Gfx> Secondary() {
    std::vector<Gfx> out;
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0,
                                ENVIRONMENT, 0));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEI2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, COMBINED, 0, ENVIRONMENT, 0));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, TEXEL0, 0, ENVIRONMENT, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0,
                                ENVIRONMENT, 0));
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEI2));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0,
                                COMBINED, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out,
         gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, SHADE, 0, 0, 0, 0, ENVIRONMENT));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE));
    PUSH(out, gDPSetCombineLERP(gfx_, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT));
    return out;
}

// DL_LUT_WALLETBOND: the one-cycle render mode and combiner the wallet's
// picture list is authored with become their two-cycle forms.
std::vector<Gfx> WalletBond() {
    std::vector<Gfx> out;
    out.push_back(F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE));
    out.push_back(F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_2CYCLE));
    out.push_back(F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, 0x00502048));
    out.push_back(F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, 0x08D02048));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE));
    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, 0, SHADE, 0, 0, 0, 0, SHADE, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED));
    return out;
}

const std::vector<Gfx>& LutFor(RoomLut lut) {
    static const std::vector<Gfx> primaryAddFog = PrimaryAddFog();
    static const std::vector<Gfx> secondaryAddFog = SecondaryAddFog();
    static const std::vector<Gfx> primary = Primary();
    static const std::vector<Gfx> secondary = Secondary();
    static const std::vector<Gfx> walletBond = WalletBond();
    switch (lut) {
        case RoomLut::PrimaryAddFog:
            return primaryAddFog;
        case RoomLut::SecondaryAddFog:
            return secondaryAddFog;
        case RoomLut::Primary:
            return primary;
        case RoomLut::Secondary:
            return secondary;
        case RoomLut::WalletBond:
            return walletBond;
    }
    return primary;
}

} // namespace

void ApplyDynamicCCRMLUT(Gfx* commands, size_t count, RoomLut lut) {
    const std::vector<Gfx>& table = LutFor(lut);
    for (size_t i = 0; i < count; i++) {
        Gfx& command = commands[i];
        for (size_t pair = 0; pair + 1 < table.size(); pair += 2) {
            if (table[pair].words.w0 == command.words.w0 && table[pair].words.w1 == command.words.w1) {
                command = table[pair + 1];
            }
        }
    }
}

} // namespace Frigate::Level
