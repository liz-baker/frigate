#include "menu/CVars.h"
#include "menu/MenuModel.h"

namespace Frigate::ConfigMenu {

namespace {

void RegisterAbout(Registry& registry) {
    const std::string tab = "Menu";
    const std::string sidebar = "About";
    registry.AddSidebar(tab, sidebar);
    registry.AddWidget(tab, sidebar, "Frigate: GoldenEye 007 on libultraship.", WidgetType::Text);
    registry.AddWidget(tab, sidebar, "Opening and closing", WidgetType::Section);
    registry.AddWidget(tab, sidebar, "Keyboard: Esc", WidgetType::Text);
    registry.AddWidget(tab, sidebar, "Controller: Back/Select (needs controller navigation, under Appearance)",
                       WidgetType::Text);
    registry.AddWidget(tab, sidebar, "Switching tabs", WidgetType::Section);
    registry.AddWidget(tab, sidebar, "Click a tab, or press L1/R1 on a controller.", WidgetType::Text);
}

void RegisterAppearance(Registry& registry) {
    const std::string tab = "Menu";
    const std::string sidebar = "Appearance";
    registry.AddSidebar(tab, sidebar);
    registry.AddWidget(tab, sidebar, "Controller navigation", WidgetType::Checkbox)
        .CVar(kControllerNavCVar)
        .Default(1)
        .Tooltip("Navigate the menu with a controller, and open it with the Back button. While the menu is open "
                 "the game doesn't see the controller.");
    registry.AddWidget(tab, sidebar, "Background opacity", WidgetType::SliderFloat)
        .CVar(kOpacityCVar)
        .Range(0.1f, 1.0f, 0.05f)
        .Default(kDefaultOpacity)
        .Tooltip("How much of the game shows through behind the menu.");
    registry.AddWidget(tab, sidebar, "Developer tools", WidgetType::Checkbox)
        .CVar(kDevToolsCVar)
        .Default(0)
        .Tooltip("Shows the Dev Tools tab, which edits save files.");
}

} // namespace

void RegisterMenuTab(Registry& registry) {
    registry.AddTab("Menu");
    RegisterAbout(registry);
    RegisterAppearance(registry);
}

} // namespace Frigate::ConfigMenu
