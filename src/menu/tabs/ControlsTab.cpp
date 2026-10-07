#include "menu/tabs/ControlsTab.h"

#include <string>
#include <utility>
#include <vector>

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

void RegisterPorts(Registry& registry, const std::string& tab, const std::string& sidebar,
                   const std::shared_ptr<System>& system) {
    const auto apply = [system] {
        if (system != nullptr) {
            system->ApplyControllerPorts();
        }
    };
    registry.AddWidget(tab, sidebar, "Controller ports", WidgetType::Section);
    registry.AddWidget(tab, sidebar,
                       "A port counts as connected when it has a device or is marked connected here; the game's "
                       "player count is the ports connected from port 1 up to the first gap. A connected port "
                       "with no device reads no input.",
                       WidgetType::Text);
    for (int port = 0; port < Platform::Input::kPortCount; port++) {
        const std::string label = "Port " + std::to_string(port + 1);
        registry.AddWidget(tab, sidebar, label + " connected", WidgetType::Checkbox)
            .CVar(kPortConnectedCVars[static_cast<size_t>(port)])
            .Tooltip("Counts this port as connected even with no device attached.")
            .Callback(apply);
        registry.AddWidget(tab, sidebar, label + " device", WidgetType::Combo)
            .CVar(kPortDeviceCVars[static_cast<size_t>(port)])
            .Default(Platform::Input::kDeviceBindings)
            .Options(Platform::Input::DeviceLabels({}, {}))
            .Tooltip("What feeds this port. \"Bindings\" leaves the bindings below as they are: by default port 1 "
                     "reads the keyboard and every gamepad, and the other ports nothing. Choosing a keyboard, "
                     "gamepad or None replaces this port's keyboard bindings, and any one device can feed more "
                     "than one port.")
            .PreFunc([system](Widget& widget) {
                // Devices() is the saved setting, so a gamepad that isn't
                // attached stays selectable instead of being dropped.
                widget.options = Platform::Input::DeviceLabels(Platform::Input::Devices(),
                                                               system != nullptr ? system->GamepadNames()
                                                                                 : std::vector<std::string>{});
            })
            .Callback(apply);
    }

    auto& first = registry.AddWidget(tab, sidebar, "Swap port", WidgetType::Combo)
                      .Options({ "Port 1", "Port 2", "Port 3", "Port 4" })
                      .Get();
    auto& second = registry.AddWidget(tab, sidebar, "With port", WidgetType::Combo)
                       .Options({ "Port 1", "Port 2", "Port 3", "Port 4" })
                       .Get();
    second.intValue = 1;
    registry.AddWidget(tab, sidebar, "Swap devices", WidgetType::Button)
        .Tooltip("Exchanges what feeds the two ports.")
        .Callback([system, &first, &second] {
            if (system != nullptr) {
                system->SwapControllerPorts(first.intValue, second.intValue);
            }
        });
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
    RegisterPorts(registry, tab, bindings, system);
    registry.AddWidget(tab, bindings, "Bindings", WidgetType::Section);
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
