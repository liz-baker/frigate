#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

#include "game/FrameTiming.h"
#include "game/MenuId.h"

union Gfx;

namespace Frigate::Game {

// Reimplements enough of the decomp's menu dispatcher
// (menu_init()/menu_jump_constructor_handler()) to drive a small, fixed
// set of menu states through their update/init/interface/constructor
// cycle -- not a general menu framework. A menu with no registered handler
// for a given callback simply does nothing for it that frame.
class MenuStateMachine {
  public:
    // update_menuXX_*, run as the menu is left: the state machine says where
    // it is going (IsSwitchingTo()).
    using UpdateFn = std::function<void(const MenuStateMachine&)>;
    // Takes the state machine so a handler can read/reset MenuTimer() and,
    // for interface handlers, call ChangeMenu().
    using InitFn = std::function<void(MenuStateMachine&)>;
    using InterfaceFn = std::function<void(MenuStateMachine&)>;
    using ConstructorFn = std::function<Gfx*(Gfx*, MenuStateMachine&)>;

    struct MenuHandlers {
        UpdateFn onUpdate;
        InitFn onInit;
        InterfaceFn onInterface;
        ConstructorFn onConstruct;
    };

    void RegisterMenu(MenuId id, MenuHandlers handlers);

    // Mirrors frontChangeMenu(): `reload=true` routes the switch through
    // the black SwitchScreens transition (what every real menu-to-menu
    // switch in the decomp does, including the very first one on boot);
    // `reload=false` switches immediately, skipping it.
    void ChangeMenu(MenuId menu, bool reload);

    // Mirrors menu_init(): call once per frame, before Constructor().
    void Tick();

    // Mirrors menu_jump_constructor_handler(): call once per frame, after
    // Tick(), to append this frame's menu content to a display list
    // already holding the black-screen clear every constructor_menuXX
    // starts with (see src/game/Game.cpp).
    Gfx* Constructor(Gfx* dl);

    // Called as a menu takes over, with the tick it happens on (counting from 0)
    // and the menu it replaces; a testing aid for following a scripted run.
    using TransitionObserver = std::function<void(uint32_t tick, MenuId from, MenuId to)>;
    void SetTransitionObserver(TransitionObserver observer) {
        transitionObserver_ = std::move(observer);
    }

    MenuId CurrentMenu() const {
        return currentMenu_;
    }

    // menu_update == menu || maybe_prev_menu == menu: a requested switch, deferred
    // or immediate, is heading for `menu`.
    bool IsSwitchingTo(MenuId menu) const {
        return pendingSwitch_ == menu || nextMenu_ == menu;
    }

    // g_MenuTimer -- frame-count elapsed since ResetMenuTimer() (which
    // every menu transition calls). Interface handlers use this to time
    // their own transitions (e.g. the legal screen's fixed-length timer).
    int MenuTimer() const {
        return menuTimer_;
    }

    void AddMenuTimer(int delta) {
        menuTimer_ += delta;
    }

    void ResetMenuTimer() {
        menuTimer_ = 0;
    }

    // g_ClockTimer: the VI ticks the current frame lasts, from the table in
    // FrameTiming.h for the phase the current menu has said it is in. Every
    // menu starts in FramePhase::Default.
    int ClockTimer() const {
        return TicksPerFrame(framePhase_);
    }

    void SetFramePhase(FramePhase phase) {
        framePhase_ = phase;
    }

  private:
    const MenuHandlers* Handlers(MenuId id) const;

    // How long the decomp's MENU_SWITCH_SCREENS blackout lasts
    // (interface_menu17_switchscreens's own hardcoded frame count) before
    // handing off to the menu that was actually requested.
    static constexpr int kSwitchScreensFrames = 4;

    std::unordered_map<MenuId, MenuHandlers> handlers_;
    MenuId currentMenu_ = MenuId::Invalid;
    std::optional<MenuId> pendingSwitch_; // menu_update
    std::optional<MenuId> nextMenu_;      // maybe_prev_menu
    int menuTimer_ = 0;                   // g_MenuTimer
    FramePhase framePhase_ = FramePhase::Default;
    // Ticks so far; the frame numbers of --input-script (platform/InputScript.h).
    uint32_t ticks_ = 0;
    TransitionObserver transitionObserver_;
};

} // namespace Frigate::Game
