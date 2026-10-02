#include "game/menu/FileSelect.h"

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"

namespace Frigate::Game::Menu {

namespace {

// 30 seconds and a frame of no input (NTSC).
constexpr int kIdleTimeout = 1801;

void Init(MenuStateMachine& menu, FrontState& front) {
    front.prevKeypresses = false;
    menu.ResetMenuTimer();
    spdlog::info("file select: back at the title; the legal screen follows after 30 seconds without input");
}

void Interface(MenuStateMachine& menu) {
    // TODO(https://github.com/liz-baker/frigate/issues/40): the stick, which
    // also resets the timer.
    if (Input::AnyButtonPressedThisFrame()) {
        menu.ResetMenuTimer();
    } else {
        menu.AddMenuTimer(menu.ClockTimer());
    }

    if (menu.MenuTimer() >= kIdleTimeout) {
        spdlog::info("file select: idle timeout, back to the legal screen");
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);
    }
}

} // namespace

void RegisterFileSelect(MenuStateMachine& menu, const std::shared_ptr<FrontState>& front) {
    menu.RegisterMenu(MenuId::FileSelect, {
                                              .onInit = [front](MenuStateMachine& m) { Init(m, *front); },
                                              .onInterface = Interface,
                                          });
}

} // namespace Frigate::Game::Menu
