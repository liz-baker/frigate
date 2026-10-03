#include "game/menu/LegalScreen.h"

#include <array>
#include <optional>

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/MenuStateMachine.h"
#include "game/Sound.h"
#include "game/TextRender.h"
#include "game/menu/PropCamera.h"
#include "game/save/SaveManager.h"
#include "model/ModelDraw.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// interface_menu00_legalscreen()'s NTSC value (PAL uses 50*4+1 instead);
// this port targets NTSC/USA only (see README).
constexpr int kLegalScreenTimerMax = 60 * 4 + 1;

// legalpage_pos, applied as the page model's root origin by setsuboffset().
constexpr std::array<float, 3> kLegalPagePos = { 0.0f, 0.0f, 0.0f };
constexpr float kLegalPageDistance = 4000.0f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD

// legalpage_text_array: positions in screen pixels, string slots in LTITLE
// (TITLE_STR_07_TWY..TITLE_STR_18_EMI).
struct LegalText {
    int32_t x;
    int32_t y;
    int32_t halign;
    int32_t valign;
    size_t slot;
};

constexpr LegalText kLegalText[] = {
    { 220, 30, Text::kCenter, Text::kCenter, 7 }, { 34, 83, Text::kLeft, Text::kCenter, 8 },
    { 226, 84, Text::kLeft, Text::kCenter, 9 },   { 226, 97, Text::kLeft, Text::kCenter, 10 },
    { 226, 110, Text::kLeft, Text::kCenter, 11 }, { 226, 122, Text::kLeft, Text::kCenter, 12 },
    { 227, 134, Text::kLeft, Text::kCenter, 13 }, { 219, 211, Text::kLeft, Text::kCenter, 14 },
    { 60, 169, Text::kLeft, Text::kCenter, 15 },  { 60, 201, Text::kLeft, Text::kCenter, 16 },
    { 99, 266, Text::kLeft, Text::kCenter, 17 },  { 80, 280, Text::kLeft, Text::kCenter, 18 },
};

// PROP_LEGALPAGE has no animation, so its model and camera are built once
// and redrawn unchanged every frame. A function-local static is the
// simplest safe home for it (the built ModelDraw's commands point at its
// matrices and at `camera`'s fields by address, so both need stable
// storage for as long as they're drawn).
struct State {
    PropCamera camera{};
    std::optional<Model::ModelDraw> draw;
    std::shared_ptr<Font> font;
    std::shared_ptr<Frigate::Text> strings;
};

State& GetState() {
    static State state;
    return state;
}

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager, FrontState& front) {
    State& state = GetState();
    front.maybeIsInMenu = true;
    spdlog::info("legal screen");
    Sound::MusicTrackStop(0);
    front.maybeIsInMenu = true;
    state.camera = BuildPropCamera(kLegalPageDistance, kAspect);
    state.draw = Model::BuildModelDraw(resourceManager, "models/prop/PlegalpageZ", state.camera.base,
                                       /*textures=*/true, PropCameraPrefix(state.camera), kLegalPagePos);

    state.font = resourceManager->LoadResource<Font>("font/fontZurichBold");
    state.strings = resourceManager->LoadResource<Frigate::Text>("obseg/text/LtitleE");
    if (state.font == nullptr || state.strings == nullptr) {
        spdlog::error("legal screen: font or LTITLE text bank missing from the loaded archives");
    }

    // Every pass through the legal screen re-reads the EEPROM.
    if (front.save != nullptr) {
        front.save->ValidateSaves();
    }
}

void Interface(MenuStateMachine& menu) {
    menu.AddMenuTimer(menu.ClockTimer());

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

    if (state.font == nullptr || state.strings == nullptr) {
        return dl;
    }
    dl = Text::SetupRdp(dl);
    for (const LegalText& entry : kLegalText) {
        dl = Text::DrawAlignedWhite(dl, entry.x, entry.y, entry.halign, entry.valign,
                                    state.strings->Strings[entry.slot], *state.font);
    }
    return dl;
}

} // namespace

void RegisterLegalScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                         const std::shared_ptr<FrontState>& front) {
    menu.RegisterMenu(MenuId::LegalScreen,
                      {
                          .onInit = [resourceManager, front](MenuStateMachine&) { Init(resourceManager, *front); },
                          .onInterface = Interface,
                          .onConstruct = Construct,
                      });
}

} // namespace Frigate::Game::Menu
