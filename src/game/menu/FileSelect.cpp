#include "game/menu/FileSelect.h"

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/Sound.h"

namespace Frigate::Game::Menu {

namespace {

// 30 seconds and a frame of no input (NTSC).
constexpr int kIdleTimeout = 1801;

void Init(MenuStateMachine& menu, FrontState& front) {
    front.prevKeypresses = false;
    if (front.maybeIsInMenu) {
        Sound::MusicTrackPlay(0, AudioDriver::kMusicFolders);
        front.maybeIsInMenu = false;
    }
    menu.ResetMenuTimer();
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
