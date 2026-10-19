#include "menu/tabs/ControlsTab.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "menu/CVars.h"
#include "menu/MenuModel.h"
#include "menu/System.h"
#include "platform/PortAssignment.h"

namespace Frigate::ConfigMenu {

namespace {

InputEditorDrawFn& InputEditorDraw() {
    static InputEditorDrawFn draw;
    return draw;
}

std::string PortLabel(int port) {
    return "Port " + std::to_string(port + 1);
}

// One port's device dropdown, its duplicate-device warning and its
// disconnected note.
void RegisterPortDevice(Registry& registry, const std::string& tab, const std::string& sidebar,
                        const std::shared_ptr<System>& system, int port) {
    const auto choices = std::make_shared<Platform::Input::DeviceChoices>();
    const auto attached = [system] {
        return system != nullptr ? system->Attached() : Platform::Input::AttachedDevices{};
    };
    const auto selected = [system, port] {
        return system != nullptr ? system->SelectedDevice(port) : Platform::Input::PortDevice{};
    };

    WidgetBuilder combo = registry.AddWidget(tab, sidebar, PortLabel(port) + " device", WidgetType::Combo);
    Widget& comboWidget = combo.Get();
    combo
        .Tooltip("What feeds this port. Default bindings leaves the bindings below as they are: port 1 reads the "
                 "keyboard, mouse and every gamepad, the other ports nothing. Choosing a device makes it the only "
                 "one this port reads. A device that is unplugged stays chosen, listed as disconnected, and the "
                 "port reads no input until it is back.")
        .PreFunc([choices, attached, selected](Widget& widget) {
            *choices = Platform::Input::BuildDeviceChoices(selected(), attached());
            widget.options.clear();
            for (const Platform::Input::DeviceChoice& choice : choices->choices) {
                widget.options.push_back(choice.label);
            }
            widget.intValue = static_cast<int>(choices->selected);
        })
        .Callback([choices, system, port, &comboWidget] {
            const auto index = static_cast<size_t>(comboWidget.intValue);
            if (system != nullptr && index < choices->choices.size()) {
                system->SelectDevice(port, choices->choices[index].device);
            }
        });

    registry.AddWidget(tab, sidebar, "", WidgetType::Text).PreFunc([attached, system, port](Widget& widget) {
        Platform::Input::PortDevices devices{};
        for (int other = 0; other < Platform::Input::kPortCount; other++) {
            devices[static_cast<size_t>(other)] =
                system != nullptr ? system->SelectedDevice(other) : Platform::Input::PortDevice{};
        }
        widget.name = Platform::Input::DuplicateDeviceWarning(devices, attached(), port);
        widget.hidden = widget.name.empty();
    });
}

// The editor draws all four port tabs itself, so each port's rows sit above it
// instead of inside its tab.
void RegisterPorts(Registry& registry, const std::string& tab, const std::string& sidebar,
                   const std::shared_ptr<System>& system) {
    const auto apply = [system] {
        if (system != nullptr) {
            system->ApplyControllerPorts();
        }
    };
    registry.AddWidget(tab, sidebar, "Ports", WidgetType::Section);
    registry.AddWidget(tab, sidebar, "", WidgetType::Text).PreFunc([](Widget& widget) {
        widget.name = Platform::Input::DeviceNotice();
        widget.hidden = widget.name.empty();
    });
    registry.AddWidget(tab, sidebar, "Dismiss notice", WidgetType::Button)
        .HiddenWhen([] { return Platform::Input::DeviceNotice().empty(); })
        .Callback([] { Platform::Input::DeviceNotice().clear(); });
    for (int port = 0; port < Platform::Input::kPortCount; port++) {
        registry.AddWidget(tab, sidebar, PortLabel(port) + " connected", WidgetType::Checkbox)
            .CVar(kPortConnectedCVars[static_cast<size_t>(port)])
            .Tooltip("Counts this port as connected even with no device attached. A connected port with no "
                     "device reads no input.")
            .Callback(apply);
        RegisterPortDevice(registry, tab, sidebar, system, port);
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

void SetInputEditorDraw(InputEditorDrawFn draw) {
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
    registry.AddWidget(tab, bindings, "Controller bindings", WidgetType::Custom).Draw([system] {
        const auto& draw = InputEditorDraw();
        if (!draw) {
            return;
        }
        std::array<std::string, Platform::Input::kPortCount> disabledReasons;
        if (system != nullptr) {
            const Platform::Input::AttachedDevices attached = system->Attached();
            for (int port = 0; port < Platform::Input::kPortCount; port++) {
                disabledReasons[static_cast<size_t>(port)] =
                    Platform::Input::DisconnectedReason(system->SelectedDevice(port), attached);
            }
        }
        draw(disabledReasons);
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
