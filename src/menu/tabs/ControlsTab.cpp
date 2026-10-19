#include "menu/tabs/ControlsTab.h"

#include <memory>
#include <string>
#include <utility>

#include "menu/MenuModel.h"
#include "menu/System.h"
#include "platform/PortAssignment.h"

namespace Frigate::ConfigMenu {

namespace {

ControlsEditorDrawFn& ControlsEditorDraw() {
    static ControlsEditorDrawFn draw;
    return draw;
}

} // namespace

std::string PlayerCountText(int players) {
    if (players <= 0) {
        return "No players: no port is connected.";
    }
    return std::to_string(players) + (players == 1 ? " player" : " players") +
           " (connected ports from port 1 up to the first gap).";
}

void SetControlsEditorDraw(ControlsEditorDrawFn draw) {
    ControlsEditorDraw() = std::move(draw);
}

void RegisterControlsTab(Registry& registry, const std::shared_ptr<System>& system) {
    const std::string tab = "Controls";
    registry.AddTab(tab);

    // Each port's tab holds its Connected checkbox, device, mode, bindings and
    // device settings (ControlsEditor.cpp).
    const std::string bindings = "Bindings";
    registry.AddSidebar(tab, bindings);
    registry.AddWidget(tab, bindings, "", WidgetType::Text).PreFunc([](Widget& widget) {
        widget.name = Platform::Input::DeviceNotice();
        widget.hidden = widget.name.empty();
    });
    registry.AddWidget(tab, bindings, "Dismiss notice", WidgetType::Button)
        .HiddenWhen([] { return Platform::Input::DeviceNotice().empty(); })
        .Callback([] { Platform::Input::DeviceNotice().clear(); });
    registry.AddWidget(tab, bindings, "Controller bindings", WidgetType::Custom).Draw([] {
        const auto& draw = ControlsEditorDraw();
        if (draw) {
            draw();
        }
    });
    registry.AddWidget(tab, bindings, "The controller editor isn't available here.", WidgetType::Text)
        .HiddenWhen([] { return static_cast<bool>(ControlsEditorDraw()); });

    registry.AddWidget(tab, bindings, "Set Defaults", WidgetType::Button)
        .Tooltip("Restores the default keyboard, mouse and gamepad bindings on every port.")
        .Confirm("This replaces the bindings on all four ports with the defaults.\n\nContinue?")
        .Callback([system] {
            if (system != nullptr) {
                system->SetDefaultBindings();
            }
        });
}

} // namespace Frigate::ConfigMenu
