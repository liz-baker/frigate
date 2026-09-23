#include "game/Game.h"

#include <cstdint>

#include "fast/Fast3dWindow.h"
#include "game/MenuStateMachine.h"
#include "game/menu/LegalScreen.h"
#include "gfx/GbiF3dex.h"
#include "ship/core/Context.h"

namespace Frigate::Game {

namespace {

// front.c's clear_framebuffer_black(), bare facts (its opcode sequence) in
// fresh wording -- SetCycleType(FILL)/SetColorImage/SetFillColor(black)/
// FillRectangle, the same recipe every constructor_menuXX starts a frame
// with. 320x240 is the decomp's own boot-time default (screen_size ==
// SCREEN_SIZE_320x240, front.c's menu_init()); real VI-driven viewport
// setup (viSetXY() et al.) isn't wired up yet.
//
// The color image is pointed at a placeholder address rather than the
// (currently unset) real framebuffer, matching src/viewer/Viewer.cpp's
// BuildClear(): a fill whose color image matches the depth buffer's
// default (zero) address gets dropped as a redundant depth clear.
Gfx* ClearFramebufferBlack(Gfx* dl) {
    using namespace Frigate::F3dex;
    constexpr int32_t kWidth = 320;
    constexpr int32_t kHeight = 240;

    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kWidth, reinterpret_cast<void*>(uintptr_t{ 0x00100000 }));
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    gDPSetFillColor(dl++, (GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1));
    gDPFillRectangle(dl++, 0, 0, kWidth - 1, kHeight - 1);
    gDPPipeSync(dl++);
    return dl;
}

MenuStateMachine BuildMenuStateMachine() {
    MenuStateMachine menu;
    Menu::RegisterLegalScreen(menu);
    // MenuId::NintendoLogo needs no registration yet: with no interface
    // handler it just stays put once reached (matching #81/#84's scope --
    // chaining on to the Rareware logo screen is later front-end work),
    // and with no constructor handler it draws as the plain black clear
    // every frame's display list already starts with. #83/#85 register
    // its real content.
    return menu;
}

} // namespace

int Run(const std::shared_ptr<Ship::Context>& context) {
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();

    MenuStateMachine menu = BuildMenuStateMachine();
    // Matches the decomp's true cold-boot path (initgamedata.c sets
    // menu_update = MENU_LEGAL_SCREEN while current_menu is still
    // MENU_INVALID) -- the legal screen appears only after the same
    // ~4-frame SwitchScreens blackout every other transition goes
    // through, not immediately.
    menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);

    while (window->IsRunning()) {
        window->HandleEvents();
        menu.Tick();

        // The black clear (a handful of commands) plus headroom for
        // #83/#85's real menu content; grow if a registered constructor
        // ever needs more.
        Gfx frame[32];
        Gfx* dl = ClearFramebufferBlack(frame);
        dl = menu.Constructor(dl);
        *dl++ = Frigate::F3dex::EndDisplayList();

        window->DrawAndRunGraphicsCommands(frame, {});
    }

    window->Close();
    return 0;
}

} // namespace Frigate::Game
