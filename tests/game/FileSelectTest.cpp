#include "game/menu/FileSelect.h"

#include <cstdio>

#include "game/FrontState.h"
#include "game/MenuStateMachine.h"

using namespace Frigate::Game;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

} // namespace

int main() {
    // Entering the file select clears the skipped-intro flag; 30 seconds and a
    // frame without input send the front-end back to the legal screen, which
    // starts the title cycle over.
    {
        auto front = std::make_shared<FrontState>();
        front->prevKeypresses = true;
        MenuStateMachine menu;
        Menu::RegisterFileSelect(menu, front);
        menu.ChangeMenu(MenuId::FileSelect, /*reload=*/false);
        menu.Tick();
        Check(menu.CurrentMenu() == MenuId::FileSelect, "the file select is current");
        Check(!front->prevKeypresses, "init_menu05_fileselect clears prev_keypresses");

        // The first tick above already ran one interface frame.
        for (int frame = 0; frame < 1798; frame++) {
            menu.Tick();
        }
        Check(menu.MenuTimer() == 1799, "sanity: timer counts one tick a frame");
        menu.Tick();
        menu.Tick();
        Check(menu.MenuTimer() == 1801, "the timer reached the timeout");
        menu.Tick();
        Check(menu.CurrentMenu() == MenuId::SwitchScreens, "the timeout switches away through the blackout");
        for (int frame = 0; frame < 4; frame++) {
            menu.Tick();
        }
        Check(menu.CurrentMenu() == MenuId::LegalScreen, "the timeout lands on the legal screen");
    }

    return gFailures == 0 ? 0 : 1;
}
