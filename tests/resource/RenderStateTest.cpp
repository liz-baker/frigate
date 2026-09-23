#include "model/RenderState.h"

#include <cstdio>
#include <vector>

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

uint8_t Opcode(const Gfx& cmd) {
    return static_cast<uint8_t>(cmd.words.w0 >> 24);
}

std::vector<Gfx> Apply(uint8_t type, const ModelRenderParams& params, bool primary) {
    std::vector<Gfx> out;
    ApplyRenderModeType(out, type, params, primary);
    return out;
}

void TestTypeZeroSetsNothing() {
    ModelRenderParams params;
    params.propType = kRenderPropViewer;
    Check(Apply(0, params, true).empty(), "model type 0 leaves the state as the list sets it");
    Check(Apply(9, params, true).empty(), "an unknown model type sets nothing");
}

void TestViewerGunLightingPrimaryAndSecondary() {
    ModelRenderParams params;
    params.propType = kRenderPropViewer;
    params.fogColour = 0x11223344;
    params.envColour = 0xAABBCCDD;

    std::vector<Gfx> primary = Apply(3, params, true);
    // pipe sync, cycle type, fog colour, env colour, combiner, render mode
    Check(primary.size() == 6, "the viewer's gun-lighting primary setup is six commands");
    Check(Opcode(primary[0]) == static_cast<uint8_t>(G_RDPPIPESYNC), "it starts with a pipe sync");
    Check(Opcode(primary[2]) == static_cast<uint8_t>(G_SETFOGCOLOR) && primary[2].words.w1 == 0x11223344,
          "the fog colour is the model's fog word");
    // Env colour: the model's RGB with a full alpha.
    Check(Opcode(primary[3]) == static_cast<uint8_t>(G_SETENVCOLOR) && primary[3].words.w1 == 0xAABBCCFF,
          "the environment colour is the model's RGB, opaque");

    std::vector<Gfx> secondary = Apply(3, params, false);
    Check(secondary.size() == 1, "the secondary setup only switches to the translucent render mode");

    ModelRenderParams withZ = params;
    withZ.zbufferEnabled = true;
    Check(Apply(3, withZ, true).back().words.w0 != primary.back().words.w0 ||
              Apply(3, withZ, true).back().words.w1 != primary.back().words.w1,
          "the z-buffer changes the render mode");
}

void TestTypeOneAndTwo() {
    ModelRenderParams params;
    std::vector<Gfx> one = Apply(1, params, true);
    Check(one.size() == 4 && Opcode(one[0]) == static_cast<uint8_t>(G_RDPPIPESYNC),
          "type 1 is sync, cycle, render mode, combiner");
    Check(Opcode(one[1]) == static_cast<uint8_t>(Fast::F3DEX_G_SETOTHERMODE_H) &&
              Opcode(one[2]) == static_cast<uint8_t>(Fast::F3DEX_G_SETOTHERMODE_L),
          "the cycle type and render mode use the indy ucode's set-other-mode commands");
    std::vector<Gfx> two = Apply(2, params, true);
    Check(two.size() == 4, "type 2 is sync, cycle, render mode, combiner");
    Check(one[2].words.w1 != two[2].words.w1 || one[2].words.w0 != two[2].words.w0,
          "the two use different render modes");
}

void TestPropTypesSelectDifferentSetups() {
    ModelRenderParams params;
    params.envColour = 0x00000080; // alpha 0x80
    std::vector<size_t> sizes;
    for (int32_t prop :
         { kRenderPropChr, kRenderPropWeapon, kRenderPropViewer, kRenderPropExplosion, kRenderPropSmoke, 0 }) {
        params.propType = prop;
        sizes.push_back(Apply(4, params, true).size());
    }
    Check(sizes[0] == 5 && sizes[1] == 6 && sizes[2] == 6 && sizes[3] == 6 && sizes[4] == 6 && sizes[5] == 5,
          "each prop type has its own type-4 setup");
}

void TestCullMode() {
    ModelRenderParams params;
    std::vector<Gfx> out;

    params.cullMode = kCullBoth;
    ApplyCullMode(out, params);
    Check(out.empty(), "the default cull mode changes nothing");

    params.cullMode = kCullNone;
    ApplyCullMode(out, params);
    params.cullMode = kCullFront;
    ApplyCullMode(out, params);
    params.cullMode = kCullBack;
    ApplyCullMode(out, params);
    Check(out.size() == 3, "none, front and back each change the geometry mode");
    Check(Opcode(out[0]) == static_cast<uint8_t>(Fast::F3DEX_G_CLEARGEOMETRYMODE) &&
              Opcode(out[1]) == static_cast<uint8_t>(Fast::F3DEX_G_SETGEOMETRYMODE),
          "none clears culling; front and back set it");
}

} // namespace

int main() {
    TestTypeZeroSetsNothing();
    TestViewerGunLightingPrimaryAndSecondary();
    TestTypeOneAndTwo();
    TestPropTypesSelectDifferentSetups();
    TestCullMode();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All render state tests passed\n");
    return 0;
}
