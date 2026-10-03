#include "game/MenuStateMachine.h"

namespace Frigate::Game {

void MenuStateMachine::RegisterMenu(MenuId id, MenuHandlers handlers) {
    handlers_[id] = std::move(handlers);
}

void MenuStateMachine::ChangeMenu(MenuId menu, bool reload) {
    if (reload) {
        pendingSwitch_ = menu;
    } else {
        nextMenu_ = menu;
    }
}

const MenuStateMachine::MenuHandlers* MenuStateMachine::Handlers(MenuId id) const {
    auto it = handlers_.find(id);
    return it == handlers_.end() ? nullptr : &it->second;
}

void MenuStateMachine::Tick() {
    struct CountTick {
        uint32_t& ticks;
        ~CountTick() {
            ticks++;
        }
    } countTick{ ticks_ };
    // menu_init()'s first switch: run the outgoing menu's update handler
    // once a switch has been requested (either a deferred one via
    // pendingSwitch_, or an immediate one already sitting in nextMenu_),
    // as long as we're not already mid-transition.
    if ((pendingSwitch_.has_value() || nextMenu_.has_value()) && currentMenu_ != MenuId::SwitchScreens) {
        if (const MenuHandlers* handlers = Handlers(currentMenu_); handlers && handlers->onUpdate) {
            handlers->onUpdate(*this);
        }
        if (pendingSwitch_.has_value()) {
            currentMenu_ = MenuId::SwitchScreens;
            ResetMenuTimer();
        }
    }

    // menu_init()'s second switch hands off to a newly-selected menu and
    // runs its init handler (init_menuXX_*).
    if (nextMenu_.has_value()) {
        if (transitionObserver_) {
            transitionObserver_(ticks_, currentMenu_, *nextMenu_);
        }
        currentMenu_ = *nextMenu_;
        nextMenu_.reset();
        framePhase_ = FramePhase::Default;
        if (const MenuHandlers* handlers = Handlers(currentMenu_); handlers && handlers->onInit) {
            handlers->onInit(*this);
        }
    }

    // menu_init()'s third switch: SwitchScreens counts its own blackout
    // down and, once it expires, converts the deferred switch into an
    // immediate one for next frame (interface_menu17_switchscreens's
    // `maybe_prev_menu = menu_update; menu_update = MENU_INVALID;`).
    // Every other menu just runs its interface handler.
    if (currentMenu_ == MenuId::SwitchScreens) {
        AddMenuTimer(1);
        if (menuTimer_ >= kSwitchScreensFrames) {
            nextMenu_ = pendingSwitch_;
            pendingSwitch_.reset();
        }
    } else if (const MenuHandlers* handlers = Handlers(currentMenu_); handlers && handlers->onInterface) {
        handlers->onInterface(*this);
    }
}

Gfx* MenuStateMachine::Constructor(Gfx* dl) {
    if (const MenuHandlers* handlers = Handlers(currentMenu_); handlers && handlers->onConstruct) {
        return handlers->onConstruct(dl, *this);
    }
    return dl;
}

} // namespace Frigate::Game
