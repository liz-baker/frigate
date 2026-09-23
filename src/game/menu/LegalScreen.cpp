#include "game/menu/LegalScreen.h"

#include <optional>

#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// interface_menu00_legalscreen()'s NTSC value (PAL uses 50*4+1 instead);
// this port targets NTSC/USA only (see README).
constexpr int kLegalScreenTimerMax = 60 * 4 + 1;

// legalpage_pos is (0,0,0) -- no offset beyond the camera's own lookat
// framing, which BuildPropCamera already provides via `base`.
constexpr float kLegalPageDistance = 4000.0f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD

// PROP_LEGALPAGE has no animation, so its model and camera are built once
// and redrawn unchanged every frame. A function-local static is the
// simplest safe home for it (the built ModelDraw's commands point at its
// matrices and at `camera`'s fields by address, so both need stable
// storage for as long as they're drawn).
struct State {
    PropCamera camera{};
    std::optional<Model::ModelDraw> draw;
};

State& GetState() {
    static State state;
    return state;
}

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    State& state = GetState();
    state.camera = BuildPropCamera(kLegalPageDistance, kAspect);
    state.draw = Model::BuildModelDraw(resourceManager, "models/prop/PlegalpageZ", state.camera.base,
                                       /*textures=*/true, PropCameraPrefix(state.camera));
}

void Interface(MenuStateMachine& menu) {
    // g_ClockTimer is normally 1; the decomp's variable-frame-rate handling
    // has no equivalent here yet.
    menu.AddMenuTimer(1);

    if (menu.MenuTimer() >= kLegalScreenTimerMax) {
        // The decomp also branches to MENU_NO_CONTROLLERS here when no
        // controller is plugged in, and skips ahead on any button press --
        // both out of scope until controller input lands (#40).
        menu.ChangeMenu(MenuId::NintendoLogo, /*reload=*/true);
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

void RegisterLegalScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    menu.RegisterMenu(MenuId::LegalScreen, {
                                               .onInit = [resourceManager]() { Init(resourceManager); },
                                               .onInterface = Interface,
                                               .onConstruct = Construct,
                                           });
}

} // namespace Frigate::Game::Menu
