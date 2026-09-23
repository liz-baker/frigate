#include "model/RenderState.h"

#include "gfx/GbiF3dex.h"

// Each RDP command is built into a temporary Gfx by the libultra macro, which
// takes the command pointer as its first argument. The macros that expand to
// the RSP's set-other-mode and geometry-mode commands aren't used: they
// number those commands as F3DEX2 does, and the indy ucode numbers them as
// F3DEX (gfx/GbiF3dex.h).
#define PUSH(out, command)     \
    do {                       \
        Gfx cmd_{};            \
        Gfx* gfx_ = &cmd_;     \
        command;               \
        (out).push_back(cmd_); \
    } while (0)

namespace Frigate {

namespace {

struct Rgba {
    uint8_t r, g, b, a;
};

Rgba Unpack(uint32_t word) {
    return { static_cast<uint8_t>(word >> 24), static_cast<uint8_t>(word >> 16), static_cast<uint8_t>(word >> 8),
             static_cast<uint8_t>(word) };
}

void PipeSync(std::vector<Gfx>& out) {
    PUSH(out, gDPPipeSync(gfx_));
}

void OneCycle(std::vector<Gfx>& out) {
    out.push_back(F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE));
}

void TwoCycle(std::vector<Gfx>& out) {
    out.push_back(F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_2CYCLE));
}

// gDPSetRenderMode().
void RenderMode(std::vector<Gfx>& out, uint32_t cycle1, uint32_t cycle2) {
    out.push_back(F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, cycle1 | cycle2));
}

void FogColor(std::vector<Gfx>& out, uint32_t word) {
    const Rgba c = Unpack(word);
    PUSH(out, gDPSetFogColor(gfx_, c.r, c.g, c.b, c.a));
}

void FogColor(std::vector<Gfx>& out, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    PUSH(out, gDPSetFogColor(gfx_, r, g, b, a));
}

void EnvColor(std::vector<Gfx>& out, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    PUSH(out, gDPSetEnvColor(gfx_, r, g, b, a));
}

void PrimColorAlpha(std::vector<Gfx>& out, uint8_t a) {
    PUSH(out, gDPSetPrimColor(gfx_, 0, 0, 0, 0, 0, a));
}

// The render modes the setups pick between, each with and without the z-buffer.
enum class Blend { Opaque, Translucent, TextureEdge };

void FogPrimRenderMode(std::vector<Gfx>& out, bool z, Blend blend) {
    switch (blend) {
        case Blend::Opaque:
            if (z) {
                RenderMode(out, G_RM_FOG_PRIM_A, G_RM_AA_ZB_OPA_SURF2);
            } else {
                RenderMode(out, G_RM_FOG_PRIM_A, G_RM_AA_OPA_SURF2);
            }
            break;
        case Blend::Translucent:
            if (z) {
                RenderMode(out, G_RM_FOG_PRIM_A, G_RM_AA_ZB_XLU_SURF2);
            } else {
                RenderMode(out, G_RM_FOG_PRIM_A, G_RM_AA_XLU_SURF2);
            }
            break;
        case Blend::TextureEdge:
            if (z) {
                RenderMode(out, G_RM_FOG_PRIM_A, G_RM_AA_ZB_TEX_EDGE2);
            } else {
                RenderMode(out, G_RM_FOG_PRIM_A, G_RM_AA_TEX_EDGE2);
            }
            break;
    }
}

void PassRenderMode(std::vector<Gfx>& out, bool z, Blend blend) {
    if (blend == Blend::Opaque) {
        if (z) {
            RenderMode(out, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
        } else {
            RenderMode(out, G_RM_PASS, G_RM_AA_OPA_SURF2);
        }
    } else {
        if (z) {
            RenderMode(out, G_RM_PASS, G_RM_AA_ZB_XLU_SURF2);
        } else {
            RenderMode(out, G_RM_PASS, G_RM_AA_XLU_SURF2);
        }
    }
}

void TriLerpModulate(std::vector<Gfx>& out) {
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
}

// Type 1: one cycle, no secondary list.
void Type1(std::vector<Gfx>& out, const ModelRenderParams& params) {
    PipeSync(out);
    OneCycle(out);
    if (params.zbufferEnabled) {
        RenderMode(out, G_RM_AA_ZB_OPA_SURF2, G_RM_AA_ZB_OPA_SURF);
    } else {
        RenderMode(out, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    }
    PUSH(out, gDPSetCombineMode(gfx_, G_CC_MODULATEIA, G_CC_MODULATEIA));
}

// Type 2: two cycles, no secondary list.
void Type2(std::vector<Gfx>& out, const ModelRenderParams& params) {
    PipeSync(out);
    TwoCycle(out);
    PassRenderMode(out, params.zbufferEnabled, Blend::Opaque);
    TriLerpModulate(out);
}

// Type 3: gun lighting, with a reduced secondary setup.
void Type3(std::vector<Gfx>& out, const ModelRenderParams& params, bool primary) {
    const bool z = params.zbufferEnabled;

    if (params.propType == kRenderPropViewer) {
        if (primary) {
            const Rgba env = Unpack(params.envColour);
            PipeSync(out);
            TwoCycle(out);
            FogColor(out, params.fogColour);
            EnvColor(out, env.r, env.g, env.b, 0xFF);
            PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, ENVIRONMENT, SHADE,
                                        ENVIRONMENT, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED));
            FogPrimRenderMode(out, z, Blend::Opaque);
        } else {
            FogPrimRenderMode(out, z, Blend::Translucent);
        }
    } else if (params.propType == kRenderPropExplosion) {
        if (primary) {
            const Rgba env = Unpack(params.envColour);
            PipeSync(out);
            TwoCycle(out);
            FogColor(out, params.fogColour);
            EnvColor(out, env.r, env.g, env.b, env.a);
            PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0,
                                        COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED));
            FogPrimRenderMode(out, z, Blend::Translucent);
        }
    } else if (params.propType == kRenderPropSmoke) {
        if ((params.envColour & 0xFF) == 0) {
            if (primary) {
                PipeSync(out);
                TwoCycle(out);
                FogColor(out, params.fogColour);
                EnvColor(out, 0xFF, 0xFF, 0xFF, 0xFF);
                PrimColorAlpha(out, (params.envColour >> 8) & 0xFF);
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION,
                                            TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, PRIMITIVE));
                FogPrimRenderMode(out, z, Blend::Opaque);
            } else {
                FogPrimRenderMode(out, z, Blend::Translucent);
            }
        } else {
            if (primary) {
                PipeSync(out);
                TwoCycle(out);
                FogColor(out, params.fogColour);
                EnvColor(out, 0, 0, 0, params.envColour & 0xFF);
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, SHADE, ENVIRONMENT,
                                            COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED));
                FogPrimRenderMode(out, z, Blend::TextureEdge);
            } else {
                PrimColorAlpha(out, (params.envColour >> 8) & 0xFF);
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, SHADE, ENVIRONMENT, TEXEL0, 0,
                                            COMBINED, 0, SHADE, 0, 1, 0, PRIMITIVE, COMBINED));
                FogPrimRenderMode(out, z, Blend::TextureEdge);
            }
        }
    } else if (params.propType == kRenderPropChr) {
        if (primary) {
            PipeSync(out);
            TwoCycle(out);
            FogColor(out, params.envColour);
            PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
            FogPrimRenderMode(out, z, Blend::Opaque);
        } else {
            FogPrimRenderMode(out, z, Blend::Translucent);
        }
    } else if (params.propType == kRenderPropWeapon) {
        const uint8_t alpha = params.envColour & 0xFF;
        if (primary) {
            PipeSync(out);
            TwoCycle(out);
            FogColor(out, params.fogColour);
            if (alpha < 255) {
                EnvColor(out, 0xFF, 0xFF, 0xFF, alpha);
                if (params.envColour & 0xFF00) {
                    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, SHADE, ENVIRONMENT, 0,
                                                COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
                } else {
                    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, ENVIRONMENT, 0,
                                                COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
                }
            } else {
                TriLerpModulate(out);
            }
            FogPrimRenderMode(out, z, Blend::Translucent);
        } else {
            if (alpha < 255) {
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL0, 0, ENVIRONMENT, 0,
                                            COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
            } else {
                TriLerpModulate(out);
            }
        }
    } else {
        if (primary) {
            PipeSync(out);
            TwoCycle(out);
            TriLerpModulate(out);
            PassRenderMode(out, z, Blend::Opaque);
        } else {
            PassRenderMode(out, z, Blend::Translucent);
        }
    }
}

// Type 4: a normal fog/lighting object.
void Type4(std::vector<Gfx>& out, const ModelRenderParams& params, bool primary) {
    const bool z = params.zbufferEnabled;
    const Blend surface = primary ? Blend::Opaque : Blend::Translucent;

    if (params.propType == kRenderPropViewer) {
        const Rgba env = Unpack(params.envColour);
        PipeSync(out);
        TwoCycle(out);
        FogColor(out, params.fogColour);
        EnvColor(out, env.r, env.g, env.b, 0xFF);
        PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, ENVIRONMENT, SHADE,
                                    ENVIRONMENT, COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED));
        FogPrimRenderMode(out, z, surface);
    } else if (params.propType == kRenderPropExplosion) {
        const Rgba env = Unpack(params.envColour);
        PipeSync(out);
        TwoCycle(out);
        FogColor(out, params.fogColour);
        EnvColor(out, env.r, env.g, env.b, env.a);
        PUSH(out, gDPSetCombineLERP(gfx_, TEXEL0, ENVIRONMENT, SHADE_ALPHA, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0,
                                    COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED));
        FogPrimRenderMode(out, z, Blend::Translucent);
    } else if (params.propType == kRenderPropSmoke) {
        if ((params.envColour & 0xFF) == 0) {
            PipeSync(out);
            TwoCycle(out);
            FogColor(out, params.fogColour);
            EnvColor(out, 0xFF, 0xFF, 0xFF, 0xFF);
            PrimColorAlpha(out, (params.envColour >> 8) & 0xFF);
            PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION,
                                        TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, PRIMITIVE));
            FogPrimRenderMode(out, z, surface);
        } else {
            PipeSync(out);
            TwoCycle(out);
            FogColor(out, params.fogColour);
            EnvColor(out, 0, 0, 0, params.envColour & 0xFF);
            if (primary) {
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, SHADE, ENVIRONMENT,
                                            COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED));
            } else {
                PrimColorAlpha(out, (params.envColour >> 8) & 0xFF);
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, SHADE, ENVIRONMENT, TEXEL0, 0,
                                            COMBINED, 0, SHADE, 0, 1, 0, PRIMITIVE, COMBINED));
            }
            FogPrimRenderMode(out, z, Blend::TextureEdge);
        }
    } else if (params.propType == kRenderPropChr) {
        PipeSync(out);
        TwoCycle(out);
        FogColor(out, params.envColour);
        PUSH(out, gDPSetCombineMode(gfx_, G_CC_TRILERP, G_CC_MODULATEIA2));
        FogPrimRenderMode(out, z, surface);
    } else if (params.propType == kRenderPropWeapon) {
        const uint8_t alpha = params.envColour & 0xFF;
        PipeSync(out);
        TwoCycle(out);
        FogColor(out, params.fogColour);
        if (alpha < 255) {
            EnvColor(out, 0xFF, 0xFF, 0xFF, alpha);
            if (primary) {
                if (params.envColour & 0xFF00) {
                    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, SHADE, ENVIRONMENT, 0,
                                                COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
                } else {
                    PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, 1, 0, ENVIRONMENT, 0,
                                                COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
                }
            } else {
                PUSH(out, gDPSetCombineLERP(gfx_, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL0, 0, ENVIRONMENT, 0,
                                            COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0));
            }
        } else {
            TriLerpModulate(out);
        }
        FogPrimRenderMode(out, z, Blend::Translucent);
    } else {
        PipeSync(out);
        TwoCycle(out);
        FogColor(out, 0xFF, 0xFF, 0xFF, 0x00);
        TriLerpModulate(out);
        FogPrimRenderMode(out, z, surface);
    }
}

} // namespace

void ApplyRenderModeType(std::vector<Gfx>& out, uint8_t modelType, const ModelRenderParams& params, bool primary) {
    switch (modelType) {
        case 1:
            Type1(out, params);
            break;
        case 2:
            Type2(out, params);
            break;
        case 3:
            Type3(out, params, primary);
            break;
        case 4:
            Type4(out, params, primary);
            break;
        default:
            break;
    }
}

void ApplyCullMode(std::vector<Gfx>& out, const ModelRenderParams& params) {
    if (params.cullMode == kCullNone) {
        out.push_back(F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH));
    } else if (params.cullMode == kCullFront) {
        out.push_back(F3dex::SetGeometryMode(Fast::F3DEX_G_CULL_FRONT));
    } else if (params.cullMode == kCullBack) {
        out.push_back(F3dex::SetGeometryMode(Fast::F3DEX_G_CULL_BACK));
    }
}

} // namespace Frigate
