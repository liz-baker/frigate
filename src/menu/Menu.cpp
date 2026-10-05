#include "menu/Menu.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "menu/CVarStore.h"
#include "menu/CVars.h"
#include "menu/MenuWindow.h"
#include "menu/SystemImpl.h"
#include "menu/Tabs.h"
#include "menu/tabs/ControlsTab.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "menu/tabs/DevToolsTab.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"

#ifndef __WIIU__
#include "libultraship/window/gui/InputEditorWindow.h"
#endif

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

    // The full-screen menu's tooltips and combo lists reach past the window's
    // edge, and the library's Metal backend crashes creating the OS window that
    // multi-viewport gives them. Keep them inside the game window unless the
    // player turned the option on.
    if (cvars->Get(kMultiViewportsCVar) == nullptr && ImGui::GetCurrentContext() != nullptr) {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }

#ifndef __WIIU__
    // The library never registers its input editor itself. Registered here and
    // left hidden, the Gui updates it every frame and the Controls tab draws
    // its content inline (DrawElement is public through GuiElement).
    if (auto controlDeck = context->GetChildren().GetFirst<Ship::ControlDeck>()) {
        auto editor =
            std::make_shared<LUS::InputEditorWindow>("gOpenWindows.InputEditor", "Input Editor", controlDeck, window);
        gui->AddGuiWindow(editor);
        std::shared_ptr<Ship::GuiElement> element = editor;
        SetInputEditorDraw([element] { element->DrawElement(); });
    }
#endif

    auto registry = std::make_shared<Registry>();
    auto system = MakeSystem(context);
    system->ApplyAudioSettings();
    system->ApplyControlStyle();
    RegisterTabs(*registry, system);
    // Needs the running game's save, so it lives with the drawing layer
    // rather than in RegisterTabs().
    RegisterDevToolsTab(*registry, reset);

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
