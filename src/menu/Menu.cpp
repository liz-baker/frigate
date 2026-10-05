#include "menu/Menu.h"

#include <spdlog/spdlog.h>

#include "menu/CVarStore.h"
#include "menu/CVars.h"
#include "menu/MenuWindow.h"
#include "menu/Tabs.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"

namespace Frigate::ConfigMenu {

void Install(const std::shared_ptr<Ship::Context>& context, std::function<void()> reset) {
    auto window = context->GetChildren().GetFirst<Ship::Window>();
    auto cvars = context->GetChildren().GetFirst<Ship::ConsoleVariable>();
    if (window == nullptr || cvars == nullptr || window->GetGui() == nullptr) {
        return;
    }
    auto gui = window->GetGui();

    if (cvars->Get(kControllerNavCVar) == nullptr) {
        cvars->SetInteger(kControllerNavCVar, 1);
    }

    auto registry = std::make_shared<Registry>();
    RegisterTabs(*registry);

    MenuActions actions;
    actions.quit = [weak = std::weak_ptr<Ship::Window>(window)] {
        if (auto locked = weak.lock()) {
            locked->Close();
        }
    };

    actions.reset = std::move(reset);

    auto menu = std::make_shared<MenuWindow>(registry, std::make_shared<CVarStore>(cvars, gui), std::move(actions));
    // SetMenu initializes the window, which looks up the Context's components.
    menu->SetContext(context);
    gui->SetMenu(menu);
    spdlog::info("menu: installed ({} tabs)", registry->Tabs().size());
}

} // namespace Frigate::ConfigMenu
