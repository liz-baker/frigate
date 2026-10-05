#include "menu/tabs/ControlsTab.h"

#include <utility>

#include "menu/CVars.h"
#include "menu/MenuModel.h"
#include "menu/System.h"

namespace Frigate::ConfigMenu {

namespace {

std::function<void()>& InputEditorDraw() {
    static std::function<void()> draw;
    return draw;
}

} // namespace

void SetInputEditorDraw(std::function<void()> draw) {
    InputEditorDraw() = std::move(draw);
}

void RegisterControlsTab(Registry& registry, const std::shared_ptr<System>& system) {
    const std::string tab = "Controls";
    registry.AddTab(tab);

    // The editor holds the per-port button and stick bindings, stick
    // sensitivity and dead zone, rumble, LED and gyro settings.
    const std::string bindings = "Bindings";
    registry.AddSidebar(tab, bindings);
    registry.AddWidget(tab, bindings, "Controller bindings", WidgetType::Custom).Draw([] {
        if (const auto& draw = InputEditorDraw()) {
            draw();
        }
    });
    registry.AddWidget(tab, bindings, "The controller editor isn't available on this platform.", WidgetType::Text)
        .HiddenWhen([] { return static_cast<bool>(InputEditorDraw()); });

    const std::string style = "Control Style";
    registry.AddSidebar(tab, style);
    registry.AddWidget(tab, style, "Control style", WidgetType::Combo)
        .CVar(kControlStyleCVar)
        .Options({ "Folder's option", "1.1", "1.2", "1.3", "1.4", "2.1", "2.2", "2.3", "2.4" })
        .Default(0)
        .Tooltip("Replaces the control style stored in the save folder while you play. \"Folder's option\" leaves "
                 "it to the game's options screen; 2.1-2.4 read a second controller.")
        .Callback([system] {
            if (system != nullptr) {
                system->ApplyControlStyle();
            }
        });
}

} // namespace Frigate::ConfigMenu
