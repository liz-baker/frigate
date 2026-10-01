#include <cstdio>

#include "gfx/GbiF3dex.h"
#include "level/BgApply.h"

using namespace Frigate::Level;
namespace F3dex = Frigate::F3dex;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Gfx RenderMode(uint32_t cycle1, uint32_t cycle2) {
    return F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, cycle1 | cycle2);
}

bool Same(const Gfx& a, const Gfx& b) {
    return a.words.w0 == b.words.w0 && a.words.w1 == b.words.w1;
}

// The words a room list carries for a terrain surface, 0x0C182078: cycle one
// pass-through, cycle two opaque terrain with the z-buffer and anti-aliasing.
void TestFogSwapsRenderModes() {
    Gfx list[3] = { RenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_TERR2), F3dex::EndDisplayList(),
                    RenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_SURF2) };
    Check(list[0].words.w0 == 0xB900031D && list[0].words.w1 == 0x0C182078,
          "the LUT's render mode words are the ones the packed room lists carry");

    ApplyDynamicCCRMLUT(list, 3, RoomLut::PrimaryAddFog);
    Check(Same(list[0], RenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_TERR2)), "an opaque surface blends into the fog");
    Check(Same(list[1], F3dex::EndDisplayList()), "other commands stay");
    Check(Same(list[2], RenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2)), "as does a translucent one");

    Gfx again[1] = { list[0] };
    ApplyDynamicCCRMLUT(again, 1, RoomLut::PrimaryAddFog);
    Check(Same(again[0], list[0]), "and patching the result again changes nothing");
}

void TestSecondaryFogLeavesOpaqueSurfacesAlone() {
    Gfx list[2] = { RenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_TERR2), RenderMode(G_RM_PASS, G_RM_AA_ZB_XLU_DECAL2) };
    ApplyDynamicCCRMLUT(list, 2, RoomLut::SecondaryAddFog);
    Check(Same(list[0], RenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_TERR2)),
          "the secondary list has no opaque surfaces to fog");
    Check(Same(list[1], RenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_DECAL2)), "but its decals blend into the fog");
}

void TestNoFogTakesAlphaFromTheEnvironment() {
    Gfx modulate;
    Gfx expected;
    {
        Gfx* pkt = &modulate;
        gDPSetCombineMode(pkt, G_CC_TRILERP, G_CC_MODULATEIA2);
        pkt = &expected;
        gDPSetCombineLERP(pkt, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, TEXEL1, TEXEL0, LOD_FRACTION, TEXEL0, COMBINED, 0,
                          SHADE, 0, COMBINED, 0, ENVIRONMENT, 0);
    }
    Gfx list[1] = { modulate };
    ApplyDynamicCCRMLUT(list, 1, RoomLut::Primary);
    Check(Same(list[0], expected), "the vertex alpha becomes the environment's");

    Gfx unrelated[1] = { RenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2) };
    ApplyDynamicCCRMLUT(unrelated, 1, RoomLut::Secondary);
    Check(Same(unrelated[0], RenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2)), "render modes are not touched");
}

void TestShadeOnlyCombiner() {
    Gfx shadeAlpha;
    Gfx environmentAlpha;
    {
        Gfx* pkt = &shadeAlpha;
        gDPSetCombineLERP(pkt, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
        pkt = &environmentAlpha;
        gDPSetCombineLERP(pkt, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT, 0, 0, 0, SHADE, 0, 0, 0, ENVIRONMENT);
    }
    Gfx list[1] = { shadeAlpha };
    ApplyDynamicCCRMLUT(list, 1, RoomLut::Primary);
    Check(Same(list[0], environmentAlpha), "a shade-only combiner takes the environment's alpha");
}

} // namespace

int main() {
    TestFogSwapsRenderModes();
    TestSecondaryFogLeavesOpaqueSurfacesAlone();
    TestNoFogTakesAlphaFromTheEnvironment();
    TestShadeOnlyCombiner();
    return gFailures == 0 ? 0 : 1;
}
