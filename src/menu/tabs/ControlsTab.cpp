#include "menu/tabs/ControlsTab.h"

#include <utility>

#include "menu/MenuModel.h"

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

void RegisterControlsTab(Registry& registry) {
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

    // TODO(https://github.com/liz-baker/frigate/issues/249): control style
    // selection; the style is a save-file option with no menu setting yet.
}

} // namespace Frigate::ConfigMenu
