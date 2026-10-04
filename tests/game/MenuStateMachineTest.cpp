#include "game/MenuStateMachine.h"

#include <cstdio>

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
    // The transition observer sees each handoff with the tick it lands on.
    {
        MenuStateMachine menu;
        uint32_t seenTick = 99;
        MenuId seenFrom = MenuId::Invalid;
        MenuId seenTo = MenuId::Invalid;
        menu.SetTransitionObserver([&](uint32_t tick, MenuId from, MenuId to) {
            seenTick = tick;
            seenFrom = from;
            seenTo = to;
        });
        menu.Tick();
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/false);
        menu.Tick();
        Check(seenTick == 1 && seenFrom == MenuId::Invalid && seenTo == MenuId::LegalScreen,
              "the observer is told the tick and both menus");
    }

    // ChangeMenu(reload=true) from the initial state doesn't switch
    // immediately -- it routes through SwitchScreens first, matching the
    // decomp's cold-boot path (menu_update set while current_menu is
    // still MENU_INVALID).
    {
        MenuStateMachine menu;
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);
        menu.Tick();
        Check(menu.CurrentMenu() == MenuId::SwitchScreens, "reload=true switch starts in SwitchScreens");
    }

    // SwitchScreens holds for exactly kSwitchScreensFrames (4) Tick()s
    // before handing off to the requested menu.
    {
        MenuStateMachine menu;
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);
        menu.Tick(); // frame 1: enters SwitchScreens, menuTimer_ == 0 going in
        for (int frame = 0; frame < 3; frame++) {
            menu.Tick();
            Check(menu.CurrentMenu() == MenuId::SwitchScreens, "still in SwitchScreens before the 4th tick");
        }
        menu.Tick(); // 4th tick since entering: timer reaches 4, hands off
        Check(menu.CurrentMenu() == MenuId::LegalScreen, "SwitchScreens hands off to the requested menu after 4 ticks");
    }

    // ChangeMenu(reload=false) switches immediately, skipping SwitchScreens.
    {
        MenuStateMachine menu;
        menu.ChangeMenu(MenuId::NintendoLogo, /*reload=*/false);
        menu.Tick();
        Check(menu.CurrentMenu() == MenuId::NintendoLogo, "reload=false switches immediately");
    }

    // ResetMenuTimer() fires when entering SwitchScreens -- a menu's own
    // in-progress timer doesn't leak into the blackout's frame count.
    {
        MenuStateMachine menu;
        menu.RegisterMenu(MenuId::LegalScreen, { .onInterface = [](MenuStateMachine& m) { m.AddMenuTimer(100); } });
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/false);
        menu.Tick(); // LegalScreen's interface handler runs, timer -> 100
        Check(menu.MenuTimer() == 100, "sanity: interface handler advanced the timer");

        menu.ChangeMenu(MenuId::NintendoLogo, /*reload=*/true);
        menu.Tick(); // enters SwitchScreens
        Check(menu.MenuTimer() == 1, "entering SwitchScreens resets the timer before counting its own frames");
    }

    // A menu's registered interface handler runs every frame it's current,
    // and can call ChangeMenu() to request the next switch -- this is
    // LegalScreen's actual timer-expiry behavior (front.c's
    // interface_menu00_legalscreen), exercised generically here.
    {
        MenuStateMachine menu;
        int interfaceCalls = 0;
        menu.RegisterMenu(MenuId::LegalScreen, { .onInterface = [&interfaceCalls](MenuStateMachine& m) {
                              interfaceCalls++;
                              if (m.MenuTimer() >= 3) {
                                  m.ChangeMenu(MenuId::NintendoLogo, /*reload=*/true);
                              }
                              m.AddMenuTimer(1);
                          } });
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/false);
        menu.Tick();
        Check(menu.CurrentMenu() == MenuId::LegalScreen, "sanity: immediate switch landed on LegalScreen");

        for (int frame = 0; frame < 3; frame++) {
            menu.Tick();
        }
        Check(interfaceCalls == 4, "interface handler ran once per tick while current");
        Check(menu.CurrentMenu() == MenuId::LegalScreen,
              "the tick that calls ChangeMenu() doesn't switch until the next tick");

        menu.Tick(); // processes the switch requested during the previous tick's interface handler
        Check(menu.CurrentMenu() == MenuId::SwitchScreens,
              "timer expiry requested a switch, which enters SwitchScreens");
    }

    // A menu with no registered handlers is a safe, silent no-op.
    {
        MenuStateMachine menu;
        menu.ChangeMenu(MenuId::NintendoLogo, /*reload=*/false);
        menu.Tick();
        menu.Tick();
        Check(menu.CurrentMenu() == MenuId::NintendoLogo, "an unregistered menu just stays put");
    }

    // Constructor() with no registered constructor returns the pointer
    // unchanged -- callers append nothing beyond whatever they already
    // wrote (e.g. the frame's black clear).
    {
        MenuStateMachine menu;
        menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/false);
        menu.Tick();
        Gfx* sentinel = reinterpret_cast<Gfx*>(0x1234);
        Check(menu.Constructor(sentinel) == sentinel, "unregistered constructor is a no-op passthrough");
    }

    // An init handler receives the machine and can reset the timer, as
    // init_menu01_nintendo() does with g_MenuTimer = 0.
    {
        MenuStateMachine menu;
        menu.AddMenuTimer(50);
        menu.RegisterMenu(MenuId::NintendoLogo, { .onInit = [](MenuStateMachine& m) { m.ResetMenuTimer(); } });
        menu.ChangeMenu(MenuId::NintendoLogo, /*reload=*/false);
        menu.Tick();
        Check(menu.MenuTimer() == 0, "an init handler can reset the menu timer");
    }

    // A menu's frame phase sets the ticks a frame lasts, and the next menu
    // starts back in the default phase.
    {
        MenuStateMachine menu;
        menu.RegisterMenu(MenuId::EyeIntro,
                          { .onInit = [](MenuStateMachine& m) { m.SetFramePhase(FramePhase::EyeIntroBond); } });
        Check(menu.ClockTimer() == 1, "a menu starts a frame a tick");
        menu.ChangeMenu(MenuId::EyeIntro, /*reload=*/false);
        menu.Tick();
        Check(menu.ClockTimer() == TicksPerFrame(FramePhase::EyeIntroBond), "the phase sets the ticks a frame lasts");
        menu.ChangeMenu(MenuId::GoldenEyeLogo, /*reload=*/false);
        menu.Tick();
        Check(menu.ClockTimer() == 1, "the next menu starts in the default phase");
    }

    Check(TicksPerFrame(FramePhase::EyeIntroSweep) == 1 && TicksPerFrame(FramePhase::EyeIntroSight) == 1 &&
              TicksPerFrame(FramePhase::EyeIntroBond) == 2,
          "the gun barrel screen's frame lengths");

    if (gFailures == 0) {
        std::printf("MenuStateMachineTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
