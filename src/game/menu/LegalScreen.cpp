#include "game/menu/LegalScreen.h"

#include "game/MenuStateMachine.h"

namespace Frigate::Game::Menu {

namespace {

// front.c's interface_menu00_legalscreen() computes this from REFRESH_PAL
// (50*4+1 on PAL, 60*4+1 on NTSC); this port targets NTSC/USA only (see
// README), so only that value is needed.
constexpr int kLegalScreenTimerMax = 60 * 4 + 1;

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

} // namespace

void RegisterLegalScreen(MenuStateMachine& menu) {
    menu.RegisterMenu(MenuId::LegalScreen, { .onInterface = Interface });
}

} // namespace Frigate::Game::Menu
