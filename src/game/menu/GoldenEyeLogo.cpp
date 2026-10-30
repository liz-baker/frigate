#include "game/menu/GoldenEyeLogo.h"

#include <array>
#include <optional>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
#include "gfx/Gu.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/ModelInstance.h"
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
    std::vector<Gfx> prefix;
    std::shared_ptr<Model::ModelInstance> logo;
    std::optional<Model::JointDraw> draw;
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
    state.prefix = PropCameraPrefix(state.camera);
    Gfx cmds[5];
    Gfx* dl = cmds;
    gSPNumLights(dl++, NUMLIGHTS_1);
    gSPLight(dl++, &state.lights.l[0], LIGHT_1);
    gSPLight(dl++, &state.lights.a, LIGHT_1 + 1);
    gSPLookAt(dl++, &state.lookAt);
    state.prefix.insert(state.prefix.end(), cmds, dl);

    state.logo = Model::ModelInstance::Create(resourceManager, "models/prop/PgoldeneyelogoZ");
    if (state.logo == nullptr) {
        state.draw.reset();
        return;
    }
    state.logo->SetRootOrigin(kLogoPos);
    state.logo->Pose(state.camera.base);

    // goldeneyelogo_MRD: no z-buffer, the default prop type and cull mode.
    ModelRenderParams params;
    params.zbufferEnabled = false;
    state.draw = Model::BuildJointDraw(resourceManager, { state.logo }, params);
}

void Update(const MenuStateMachine&) {
    State& state = GetState();
    state.front->isFirstTimeOnMainMenu = false;
    state.draw.reset();
    state.logo.reset();
}

void Interface(MenuStateMachine& menu) {
    FrontState& front = *GetState().front;
    menu.AddMenuTimer(menu.ClockTimer());
    const int timer = menu.MenuTimer();

    if (!front.isFirstTimeOnMainMenu || timer > kTimer1 || (front.geLogoBool && timer > kTimer2)) {
        if (timer > kTimer1) {
            if (front.prevKeypresses) {
                menu.SwitchMenu(MenuId::FileSelect, /*reload=*/true);
            } else {
                // do_extended_cast_display(FALSE)
                front.fullActorIntro = false;
                front.introCharacterIndex = 1;
                menu.SwitchMenu(MenuId::DisplayCast, /*reload=*/true);
            }
        } else if (Input::AnyButtonPressedThisFrame() || (front.isFirstTimeOnMainMenu && front.geLogoBool)) {
            menu.SwitchMenu(MenuId::FileSelect, /*reload=*/true);
        }
    } else if (Input::AnyButtonPressedThisFrame()) {
        front.geLogoBool = true;
    }
}

Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    const State& state = GetState();
    if (!state.draw) {
        // The model's load already logged why; draw nothing beyond the
        // frame's own black clear.
        return dl;
    }
    for (const Gfx& command : state.prefix) {
        *dl++ = command;
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
