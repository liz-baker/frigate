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
}

// A live reference for each widget type; the code is the example docs/menu.md
// points at.
void RegisterWidgets(Registry& registry) {
    const std::string tab = "Menu";
    const std::string sidebar = "Widgets";
    registry.AddSidebar(tab, sidebar, 2);

    registry.AddWidget(tab, sidebar, "Basic controls", WidgetType::Section);
    registry.AddWidget(tab, sidebar, "Example checkbox", WidgetType::Checkbox).Tooltip("Not stored anywhere.");
    registry.AddWidget(tab, sidebar, "Example integer slider", WidgetType::SliderInt).Range(0, 10).Default(5);
    registry.AddWidget(tab, sidebar, "Example float slider", WidgetType::SliderFloat)
        .Range(0.0f, 2.0f, 0.1f)
        .Default(1.0f)
        .Format("%.1fx");
    registry.AddWidget(tab, sidebar, "Example combo", WidgetType::Combo)
        .Options({ "First", "Second", "Third" })
        .Default(1);
    registry.AddWidget(tab, sidebar, "Example button", WidgetType::Button);
    registry.AddWidget(tab, sidebar, "Example disabled checkbox", WidgetType::Checkbox)
        .Disabled("Disabled widgets say why when hovered.");

    registry.AddWidget(tab, sidebar, "Containers", WidgetType::Section).Column(1);
    auto header =
        registry.AddWidget(tab, sidebar, "Example collapsible header", WidgetType::Header).Column(1).DefaultOpen();
    header.Child("Inside the header", WidgetType::Checkbox);
    auto group = registry.AddWidget(tab, sidebar, "Example boxed group", WidgetType::Group).Column(1);
    group.Child("Inside the group", WidgetType::Checkbox);
    group.Child("Another slider in the group", WidgetType::SliderInt).Range(0, 100, 5).Default(50);
}

} // namespace

void RegisterMenuTab(Registry& registry) {
    registry.AddTab("Menu");
    RegisterAbout(registry);
    RegisterAppearance(registry);
    RegisterWidgets(registry);
}

} // namespace Frigate::ConfigMenu
