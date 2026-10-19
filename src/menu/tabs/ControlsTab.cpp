#include "menu/tabs/ControlsTab.h"

#include <string>
#include <utility>

#include "menu/CVars.h"
#include "menu/MenuModel.h"
#include "menu/System.h"
#include "platform/PortAssignment.h"

namespace Frigate::ConfigMenu {

namespace {

std::function<void()>& InputEditorDraw() {
    static std::function<void()> draw;
    return draw;
}

// The editor draws all four port tabs itself, so each port's Connected
// checkbox sits in a row above it instead of inside its tab.
void RegisterPorts(Registry& registry, const std::string& tab, const std::string& sidebar,
                   const std::shared_ptr<System>& system) {
    const auto apply = [system] {
        if (system != nullptr) {
            system->ApplyControllerPorts();
        }
    };
    registry.AddWidget(tab, sidebar, "Ports", WidgetType::Section);
    for (int port = 0; port < Platform::Input::kPortCount; port++) {
        registry.AddWidget(tab, sidebar, "Port " + std::to_string(port + 1) + " connected", WidgetType::Checkbox)
            .CVar(kPortConnectedCVars[static_cast<size_t>(port)])
            .Tooltip("Counts this port as connected even with no device attached. A connected port with no "
                     "device reads no input.")
            .Callback(apply);
    }
    registry.AddWidget(tab, sidebar, "", WidgetType::Text).PreFunc([](Widget& widget) {
        widget.name = PlayerCountText(Platform::Input::ControllerCount());
    });
}

} // namespace

std::string PlayerCountText(int players) {
    if (players <= 0) {
        return "No players: no port is connected.";
    }
    return std::to_string(players) + (players == 1 ? " player" : " players") +
           " (connected ports from port 1 up to the first gap).";
}

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
    RegisterPorts(registry, tab, bindings, system);
    registry.AddWidget(tab, bindings, "Bindings", WidgetType::Section);
    registry.AddWidget(tab, bindings, "Controller bindings", WidgetType::Custom).Draw([] {
        if (const auto& draw = InputEditorDraw()) {
            draw();
        }
    });
    registry.AddWidget(tab, bindings, "The controller editor isn't available on this platform.", WidgetType::Text)
        .HiddenWhen([] { return static_cast<bool>(InputEditorDraw()); });

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
