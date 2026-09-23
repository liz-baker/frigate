#include "game/menu/LegalScreen.h"

#include <optional>

#include "game/MenuStateMachine.h"
#include "game/menu/PropCamera.h"
#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// front.c's interface_menu00_legalscreen() computes this from REFRESH_PAL
// (50*4+1 on PAL, 60*4+1 on NTSC); this port targets NTSC/USA only (see
// README), so only that value is needed.
constexpr int kLegalScreenTimerMax = 60 * 4 + 1;

// legalpage_pos (front.c) is (0,0,0) -- no offset beyond the camera's own
// lookat framing, which BuildPropCamera already provides via `base`.
constexpr float kLegalPageDistance = 4000.0f;
constexpr float kAspect = 320.0f / 240.0f;

// PROP_LEGALPAGE has no animation, so its model and camera are built once
// (onInit, mirroring init_menu00_legalscreen's modelmgrInstantiateModel
// call) and redrawn unchanged every frame -- state needs to survive across
// Tick()/Constructor() calls, and this state machine only ever has one
// legal screen, so a function-local static is the simplest safe home for
// it (its address must be stable: the built ModelDraw's commands point
// into its own matrices by address, and into `camera`'s fields).
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
    // g_MenuTimer += g_ClockTimer -- g_ClockTimer is the number of 60Hz
    // ticks the frame accounts for, normally 1; the decomp's own
    // variable-frame-rate handling (speedgraphframes, lv.c) has no
    // equivalent here yet.
    menu.AddMenuTimer(1);

    if (menu.MenuTimer() >= kLegalScreenTimerMax) {
        // The decomp also branches to MENU_NO_CONTROLLERS here when no
        // controller is plugged in on first boot -- out of scope until
        // controller input lands (#40).
        menu.ChangeMenu(MenuId::NintendoLogo, /*reload=*/true);
    }

    // The decomp's other branch -- skipping ahead on any button press --
    // is also #40's, not this one's.
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
