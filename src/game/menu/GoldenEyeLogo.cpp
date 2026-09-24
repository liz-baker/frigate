#include "game/menu/GoldenEyeLogo.h"

#include <array>
#include <optional>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
#include "gfx/Gu.h"
#include "libultraship/libultra/gbi.h"
#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// interface_menu04_goldeneyelogo()'s NTSC values (PAL uses 50-based ones);
// this port targets NTSC/USA only (see README).
constexpr int kTimer1 = 60 * 3;
constexpr int kTimer2 = (60 / 2) * 3;

constexpr float kLogoDistance = 3000.0f;
// matrix_scalar_multiply(1.2f, ...) on the lookat matrix's first three rows.
constexpr float kLogoScale = 1.2f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD

// goldeneyelogo_pos, applied as the logo model's root origin by setsuboffset().
constexpr std::array<float, 3> kLogoPos = { 0.0f, 0.0f, 0.0f };

// The reflection look-at's eye and target: a fixed 4000-unit Z lookat,
// independent of the camera the model is drawn from.
constexpr float kReflectEyeZ = 4000.0f;

struct State {
    std::shared_ptr<FrontState> front;
    PropCamera camera{};
    // Both are DMA'd from by address (gSPLight/gSPLookAt), so they need the
    // same stable storage as `camera`'s fields.
    Lights1 lights = gdSPDefLights1(0x96, 0x96, 0x96, 0xFF, 0xFF, 0xFF, 77, 77, 46); // gelogolight
    LookAt lookAt{};
    std::optional<Model::ModelDraw> draw;
};

State& GetState() {
    static State state;
    return state;
}

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager, MenuStateMachine& menu) {
    State& state = GetState();
    menu.ResetMenuTimer();

    state.camera = BuildPropCamera(kLogoDistance, kAspect, kLogoScale);
    Gu::LookAtReflect(state.lookAt, 0.0f, 0.0f, kReflectEyeZ, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // gSPSetLights1(gelogolight) and gSPLookAt(), written out as individual
    // commands (see NintendoLogo.cpp for why the macros aren't used directly).
    std::vector<Gfx> prefix = PropCameraPrefix(state.camera);
    Gfx cmds[5];
    Gfx* dl = cmds;
    gSPNumLights(dl++, NUMLIGHTS_1);
    gSPLight(dl++, &state.lights.l[0], LIGHT_1);
    gSPLight(dl++, &state.lights.a, LIGHT_1 + 1);
    gSPLookAt(dl++, &state.lookAt);
    prefix.insert(prefix.end(), cmds, dl);

    state.draw = Model::BuildModelDraw(resourceManager, "models/prop/PgoldeneyelogoZ", state.camera.base,
                                       /*textures=*/true, prefix, kLogoPos);
}

void Update() {
    State& state = GetState();
    state.front->isFirstTimeOnMainMenu = false;
    state.draw.reset();
}

void Interface(MenuStateMachine& menu) {
    FrontState& front = *GetState().front;
    menu.AddMenuTimer(menu.ClockTimer());
    const int timer = menu.MenuTimer();

    if (!front.isFirstTimeOnMainMenu || timer > kTimer1 || (front.geLogoBool && timer > kTimer2)) {
        if (timer > kTimer1) {
            if (front.prevKeypresses) {
                menu.ChangeMenu(MenuId::FileSelect, /*reload=*/true);
            } else {
                // do_extended_cast_display(FALSE)
                front.fullActorIntro = false;
                front.introCharacterIndex = 1;
                menu.ChangeMenu(MenuId::DisplayCast, /*reload=*/true);
            }
        } else if (Input::AnyButtonPressedThisFrame() || (front.isFirstTimeOnMainMenu && front.geLogoBool)) {
            menu.ChangeMenu(MenuId::FileSelect, /*reload=*/true);
        }
    } else if (Input::AnyButtonPressedThisFrame()) {
        front.geLogoBool = true;
    }
}

Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    const State& state = GetState();
    if (!state.draw) {
        // BuildModelDraw already logged why; draw nothing beyond the
        // frame's own black clear.
        return dl;
    }
    for (const Gfx& command : state.draw->commands) {
        *dl++ = command;
    }
    return dl;
}

} // namespace

void RegisterGoldenEyeLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front) {
    GetState().front = front;
    menu.RegisterMenu(MenuId::GoldenEyeLogo,
                      {
                          .onUpdate = Update,
                          .onInit = [resourceManager](MenuStateMachine& m) { Init(resourceManager, m); },
                          .onInterface = Interface,
                          .onConstruct = Construct,
                      });
}

} // namespace Frigate::Game::Menu
