#include "menu/tabs/SettingsTab.h"

#include "menu/CVars.h"
#include "menu/SettingsModel.h"

namespace Frigate::ConfigMenu {

namespace {

constexpr const char* kTab = "Settings";
constexpr const char* kSidebar = "General";

void RegisterAbout(Registry& registry, const std::shared_ptr<System>& system) {
    registry.AddWidget(kTab, kSidebar, "About", WidgetType::Section).Column(1);
    registry.AddWidget(kTab, kSidebar, "Frigate", WidgetType::Text).Column(1);
    registry.AddWidget(kTab, kSidebar, VersionLabel(system->Version()), WidgetType::Text).Column(1);
    registry.AddWidget(kTab, kSidebar, "GoldenEye 007 (US, NTSC-U)", WidgetType::Text).Column(1);
}

} // namespace

void RegisterSettingsTab(Registry& registry, const std::shared_ptr<System>& system) {
    registry.AddTab(kTab);
    registry.AddSidebar(kTab, kSidebar, 2);

    registry.AddWidget(kTab, kSidebar, "Menu Settings", WidgetType::Section);
    registry.AddWidget(kTab, kSidebar, "Menu Theme", WidgetType::Combo)
        .CVar(kThemeCVar)
        .Options(std::vector<std::string>(kMenuThemes.begin(), kMenuThemes.end()))
        .Tooltip("The colours of the menu.");
    registry.AddWidget(kTab, kSidebar, "Menu Controller Navigation", WidgetType::Checkbox)
        .CVar(kControllerNavCVar)
        .Default(1)
        .Tooltip("Navigate the menu with a controller, and open it with the Back button. While the menu is open "
                 "the game doesn't see the controller.");
    registry.AddWidget(kTab, kSidebar, "Menu Background Opacity", WidgetType::SliderFloat)
        .CVar(kOpacityCVar)
        .Range(0.1f, 1.0f, 0.05f)
        .Default(kDefaultOpacity)
        .Tooltip("How much of the game shows through behind the menu.");

    registry.AddWidget(kTab, kSidebar, "General Settings", WidgetType::Section);
    registry.AddWidget(kTab, kSidebar, "Search In Sidebar", WidgetType::Checkbox)
        .CVar(kSearchInSidebarCVar)
        .Tooltip("Put the search box above the sidebar list instead of in the top bar.");
    registry.AddWidget(kTab, kSidebar, "Search Input Autofocus", WidgetType::Checkbox)
        .CVar(kSearchAutofocusCVar)
        .Tooltip("Start typing into the search box as soon as the menu opens.");
    auto openFolder = registry.AddWidget(kTab, kSidebar, "Open App Files Folder", WidgetType::Button)
                          .Tooltip("Opens the folder holding the config file, logs and game archives.")
                          .Callback([system] { system->OpenAppFolder(); });
    openFolder.PreFunc([system](Widget& widget) {
        const std::string reason = system->OpenAppFolderUnavailableReason();
        if (!reason.empty()) {
            widget.disabled = true;
            widget.disabledReason = reason;
        }
    });

    RegisterAbout(registry, system);
    RegisterSettingsAudio(registry, system);
    RegisterSettingsGraphics(registry, system);
}

} // namespace Frigate::ConfigMenu
