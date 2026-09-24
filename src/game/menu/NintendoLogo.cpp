#include "game/menu/NintendoLogo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
#include "libultraship/libultra/gbi.h"
#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// interface_menu01_nintendo()'s NTSC value (~8.32 * 60 + 1; PAL uses 417);
// this port targets NTSC/USA only (see README).
constexpr int kNintendoTimerMax = 501;

constexpr float kNintendoLogoDistance = 4000.0f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD

// nintendologo_pos, applied as the logo model's root origin by setsuboffset().
constexpr std::array<float, 3> kNintendoLogoPos = { 0.0f, 0.0f, 0.0f };

// init_menu01_nintendo's starting values.
constexpr float kInitialRotation = -1.39626348019f;
constexpr float kInitialScale = 0.0183333326131f;
// constructor_menu01_nintendo's per-tick increments (NTSC; PAL differs
// slightly, out of scope -- NTSC/USA only, see README).
constexpr float kRotationStep = 0.017453292f;
constexpr float kScaleGrowth = 1.07977f;
constexpr float kMaxScale = 1.1f;

// constructor_menu01_nintendo's ambient-light formula (NTSC): clamps at
// full brightness for the first few hundred frames, then fades down as
// the screen approaches its own timeout.
uint8_t AmbientLevel(int menuTimer) {
    int32_t level = 0xFF - ((static_cast<int32_t>(menuTimer) * 0xFF + static_cast<int32_t>(0xFFFE8F72)) / 100);
    return static_cast<uint8_t>(std::clamp(level, 0, 0xFF));
}

// PROP_NINTENDOLOGO spins in place, so unlike LegalScreen its ModelDraw is
// rebuilt every Construct() call. State must outlive the call (the
// interpreter consumes `commands` after Constructor() returns), so it's
// kept here rather than as a local.
struct State {
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    std::shared_ptr<FrontState> front;
    PropCamera camera{};
    float rotation = kInitialRotation;
    float scale = kInitialScale;
    // PROP_NINTENDOLOGO relies on the RSP's lighting pipeline for its
    // shading, unlike PROP_LEGALPAGE's baked vertex colors -- needs a
    // stable address like `camera`'s fields (gSPLight DMAs from it).
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

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager, MenuStateMachine& menu) {
    State& state = GetState();
    menu.ResetMenuTimer();
    state.resourceManager = resourceManager;
    state.camera = BuildPropCamera(kNintendoLogoDistance, kAspect);
    state.rotation = kInitialRotation;
    state.scale = kInitialScale;
    state.draw.reset();

    // ninlogolight's static gdSPDefLights1() values: the directional
    // light's color and direction are both zero, so only the ambient
    // component (set every frame in Construct(), from AmbientLevel())
    // actually contributes.
    state.lights.l[0].l.dir[0] = 0;
    state.lights.l[0].l.dir[1] = 0;
    state.lights.l[0].l.dir[2] = 0;
    for (int i = 0; i < 3; i++) {
        state.lights.l[0].l.col[i] = 0;
        state.lights.l[0].l.colc[i] = 0;
    }
}

void Update() {
    GetState().draw.reset();
}

void Interface(MenuStateMachine& menu) {
    FrontState& front = *GetState().front;
    menu.AddMenuTimer(kClockTimer);

    if (menu.MenuTimer() >= kNintendoTimerMax) {
        menu.ChangeMenu(MenuId::RarewareLogo, /*reload=*/true);
    } else if (Input::AnyButtonPressedThisFrame()) {
        if (!front.isFirstTimeOnMainMenu) {
            menu.ChangeMenu(MenuId::FileSelect, /*reload=*/true);
        } else {
            front.prevKeypresses = true;
            menu.ChangeMenu(MenuId::RarewareLogo, /*reload=*/true);
        }
    }
}

Gfx* Construct(Gfx* dl, MenuStateMachine& menu) {
    State& state = GetState();

    uint8_t ambient = AmbientLevel(menu.MenuTimer());
    for (int i = 0; i < 3; i++) {
        state.lights.a.l.col[i] = ambient;
        state.lights.a.l.colc[i] = ambient;
    }

    // The spin and growth step once per 60 Hz tick, not per frame.
    for (int tick = 0; tick < kClockTimer; tick++) {
        state.rotation += kRotationStep;
        state.scale = std::min(state.scale * kScaleGrowth, kMaxScale);
    }

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

    // gSPSetLights1(name) written out as individual commands: its own
    // definition assumes a self-advancing packet pointer, which this
    // codebase's dl++ convention doesn't provide.
    std::vector<Gfx> prefix = PropCameraPrefix(state.camera);
    Gfx lightCmds[3];
    Gfx* lp = lightCmds;
    gSPNumLights(lp++, NUMLIGHTS_1);
    gSPLight(lp++, &state.lights.l[0], LIGHT_1);
    gSPLight(lp++, &state.lights.a, LIGHT_1 + 1);
    prefix.insert(prefix.end(), lightCmds, lightCmds + 3);

    auto draw = Model::BuildModelDraw(state.resourceManager, "models/prop/PnintendologoZ", base, /*textures=*/true,
                                      prefix, kNintendoLogoPos);
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

void RegisterNintendoLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                          const std::shared_ptr<FrontState>& front) {
    GetState().front = front;
    menu.RegisterMenu(MenuId::NintendoLogo,
                      {
                          .onUpdate = Update,
                          .onInit = [resourceManager](MenuStateMachine& m) { Init(resourceManager, m); },
                          .onInterface = Interface,
                          .onConstruct = Construct,
                      });
}

} // namespace Frigate::Game::Menu
