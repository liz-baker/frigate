#include "game/menu/NintendoLogo.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
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
}

Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();

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

    auto draw = Model::BuildModelDraw(state.resourceManager, "models/prop/PnintendologoZ", base, /*textures=*/true,
                                      PropCameraPrefix(state.camera));
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
