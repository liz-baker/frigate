#include "game/menu/NintendoLogo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
#include "libultraship/libultra/gbi.h"
#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr float kNintendoLogoDistance = 4000.0f;
constexpr float kAspect = 320.0f / 240.0f;

// init_menu01_nintendo's starting values (front.c).
constexpr float kInitialRotation = -1.39626348019f;
constexpr float kInitialScale = 0.0183333326131f;
// constructor_menu01_nintendo's per-frame increments (NTSC; the decomp's
// REFRESH_PAL build uses slightly different constants -- see #82's US-only
// note, same rationale).
constexpr float kRotationStep = 0.017453292f;
constexpr float kScaleGrowth = 1.07977f;
constexpr float kMaxScale = 1.1f;

// constructor_menu01_nintendo's own ambient-light formula (NTSC): starts
// clamped at full brightness and only fades down as the screen approaches
// its own timeout (the EU build's constants differ slightly, same #82
// rationale for sticking to NTSC/USA only). g_MenuTimer's amplitude here
// (roughly 0..500) means the additive term starts deeply negative, so
// `0xFF - term` clamps to 255 for a few hundred frames before it starts
// dropping -- a bare arithmetic fact, transcribed in fresh code.
uint8_t AmbientLevel(int menuTimer) {
    int32_t level = 0xFF - ((static_cast<int32_t>(menuTimer) * 0xFF + static_cast<int32_t>(0xFFFE8F72)) / 100);
    return static_cast<uint8_t>(std::clamp(level, 0, 0xFF));
}

// PROP_NINTENDOLOGO spins in place, so unlike LegalScreen its ModelDraw is
// rebuilt every Construct() call with an updated rotation -- state still
// needs to survive from the moment it's built to when the interpreter
// actually consumes `commands` (Game.cpp's DrawAndRunGraphicsCommands,
// called after Constructor() returns), so it's kept here rather than as a
// Construct()-local temporary.
struct State {
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    PropCamera camera{};
    float rotation = kInitialRotation;
    float scale = kInitialScale;
    // constructor_menu01_nintendo sets up real light data before drawing
    // (gSPNumLights/gSPLight); constructor_menu00_legalscreen never does,
    // matching that PROP_LEGALPAGE's geometry carries its own baked vertex
    // colors while PROP_NINTENDOLOGO relies on the RSP's lighting pipeline
    // for its shading -- without this, its vertices compute to black
    // against the black clear: loads and "draws" with no error, never
    // visible. Needs a stable address like `camera`'s fields (the gSPLight
    // commands DMA from it by pointer).
    Lights1 lights{};
    std::optional<Model::ModelDraw> draw;
};

State& GetState() {
    static State state;
    return state;
}

// Row-vector 4x4 multiply: result = a * b.
Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[i][k] * b.m[k][j];
            }
            result.m[i][j] = sum;
        }
    }
    return result;
}

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    State& state = GetState();
    state.resourceManager = resourceManager;
    state.camera = BuildPropCamera(kNintendoLogoDistance, kAspect);
    state.rotation = kInitialRotation;
    state.scale = kInitialScale;
    state.draw.reset();

    // ninlogolight's static gdSPDefLights1() values (front.c): the
    // directional light's color and direction are both zero, so only the
    // ambient component (set every frame in Construct(), from the real
    // timer-driven formula) actually contributes -- a bare fact, not
    // adapted code.
    state.lights.l[0].l.dir[0] = 0;
    state.lights.l[0].l.dir[1] = 0;
    state.lights.l[0].l.dir[2] = 0;
    for (int i = 0; i < 3; i++) {
        state.lights.l[0].l.col[i] = 0;
        state.lights.l[0].l.colc[i] = 0;
    }
}

Gfx* Construct(Gfx* dl, MenuStateMachine& menu) {
    State& state = GetState();

    uint8_t ambient = AmbientLevel(menu.MenuTimer());
    for (int i = 0; i < 3; i++) {
        state.lights.a.l.col[i] = ambient;
        state.lights.a.l.colc[i] = ambient;
    }

    state.rotation += kRotationStep;
    state.scale = std::min(state.scale * kScaleGrowth, kMaxScale);

    const float c = std::cos(state.rotation);
    const float s = std::sin(state.rotation);
    // Y-axis rotation, scaled uniformly in the 3x3 block only (matches
    // matrix_4x4_set_rotation_around_y() + matrix_scalar_multiply_3()) --
    // applied as a model-local transform before the camera's own lookat.
    Mat4 rotateScale{ { { state.scale * c, 0, -state.scale * s, 0 },
                        { 0, state.scale, 0, 0 },
                        { state.scale * s, 0, state.scale * c, 0 },
                        { 0, 0, 0, 1 } } };
    Mat4 base = Multiply(rotateScale, state.camera.base);

    // Lights (matches gSPSetLights1(name), written out as individual
    // caller-advances-the-pointer commands to match this codebase's own
    // convention -- gSPSetLightsN's own definition assumes a
    // self-advancing packet pointer, which the Gfx-array style used
    // throughout src/ doesn't).
    std::vector<Gfx> prefix = PropCameraPrefix(state.camera);
    Gfx lightCmds[3];
    Gfx* lp = lightCmds;
    gSPNumLights(lp++, NUMLIGHTS_1);
    gSPLight(lp++, &state.lights.l[0], LIGHT_1);
    gSPLight(lp++, &state.lights.a, LIGHT_1 + 1);
    prefix.insert(prefix.end(), lightCmds, lightCmds + 3);

    auto draw =
        Model::BuildModelDraw(state.resourceManager, "models/prop/PnintendologoZ", base, /*textures=*/true, prefix);
    if (!draw) {
        // BuildModelDraw already logged why; draw nothing beyond the
        // frame's own black clear.
        return dl;
    }
    state.draw = std::move(*draw);

    for (const Gfx& command : state.draw->commands) {
        *dl++ = command;
    }
    return dl;
}

} // namespace

void RegisterNintendoLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    menu.RegisterMenu(MenuId::NintendoLogo, {
                                                .onInit = [resourceManager]() { Init(resourceManager); },
                                                .onConstruct = Construct,
                                            });
}

} // namespace Frigate::Game::Menu
