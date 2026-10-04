#include "menu/SettingsModel.h"
#include "menu/tabs/SettingsTab.h"

namespace Frigate::ConfigMenu {

void RegisterSettingsAudio(Registry& registry, const std::shared_ptr<System>& system) {
    const std::string tab = "Settings";
    const std::string sidebar = "Audio";
    registry.AddSidebar(tab, sidebar);

    registry.AddWidget(tab, sidebar, "Output", WidgetType::Section);

    // libultraship has no volume control, and the game's own sound and music
    // volumes live in its Options screen.
    // TODO(https://github.com/liz-baker/frigate/issues/250): a master volume
    // once there is somewhere in the audio path to apply it.
    registry.AddWidget(tab, sidebar, "Master Volume", WidgetType::SliderInt)
        .Range(0, 100, 5)
        .Default(100)
        .Format("%d%%")
        .Disabled("Not implemented yet. The game's own sound and music volumes are in its Options screen.");

    // Switching the backend re-creates the player the game's sound thread
    // already holds, so the choice is saved and applied on the next launch.
    auto backends = std::make_shared<std::vector<Choice>>(system->AudioBackends());
    auto combo = registry.AddWidget(tab, sidebar, std::string("Audio Backend") + kRestartSuffix, WidgetType::Combo)
                     .Options(ChoiceLabels(*backends))
                     .Tooltip("The library that plays the game's sound. Takes effect the next time the game starts.");
    Widget* widget = &combo.Get();
    combo.PreFunc([system, backends](Widget& w) {
        w.intValue = ChoiceIndex(*backends, system->AudioBackend());
        if (backends->size() < 2) {
            w.disabled = true;
            w.disabledReason = "No other audio backend is available on this platform.";
        }
    });
    combo.Callback([system, backends, widget] {
        system->SetAudioBackend(ChoiceId(*backends, widget->intValue, system->AudioBackend()));
    });
}

} // namespace Frigate::ConfigMenu
