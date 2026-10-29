#include "menu/Menu.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "gfx/DisplayAspect.h"
#include "menu/ControlsEditor.h"
#include "menu/CVarStore.h"
#include "menu/CVars.h"
#include "menu/MenuWindow.h"
#include "menu/SystemImpl.h"
#include "menu/Tabs.h"
#include "menu/tabs/ControlsTab.h"
#include "menu/tabs/DevToolsTab.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"

namespace Frigate::ConfigMenu {

void Install(const std::shared_ptr<Ship::Context>& context, std::function<void()> reset,
             std::function<void(const Game::MultiplayerSettings&)> startMultiplayer) {
    auto window = context->GetChildren().GetFirst<Ship::Window>();
    auto cvars = context->GetChildren().GetFirst<Ship::ConsoleVariable>();
    if (window == nullptr || cvars == nullptr || window->GetGui() == nullptr) {
        return;
    }
    auto gui = window->GetGui();

    if (cvars->Get(kControllerNavCVar) == nullptr) {
        cvars->SetInteger(kControllerNavCVar, 1);
    }

    // The full-screen menu's tooltips and combo lists reach past the window's
    // edge, and the library's Metal backend crashes creating the OS window that
    // multi-viewport gives them. Keep them inside the game window unless the
    // player turned the option on.
    if (cvars->Get(kMultiViewportsCVar) == nullptr && ImGui::GetCurrentContext() != nullptr) {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }

    auto registry = std::make_shared<Registry>();
    auto system = MakeSystem(context);
    system->ApplyRenderSettings();
    Display::SetLivePlayChangedHandler([system] { system->ApplyRenderSettings(); });
    system->ApplyAudioSettings();
    system->ApplyControllerPorts();
    SetControlsEditorDraw([system] { DrawControlsEditor(*system); });
    RegisterTabs(*registry, system);
    // Needs the running game's save, so it lives with the drawing layer
    // rather than in RegisterTabs().
    RegisterDevToolsTab(*registry, reset, std::move(startMultiplayer));

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
