#include "menu/CVars.h"
#include "menu/SettingsModel.h"
#include "menu/tabs/SettingsTab.h"

namespace Frigate::ConfigMenu {

void RegisterSettingsGraphics(Registry& registry, const std::shared_ptr<System>& system) {
    const std::string tab = "Settings";
    const std::string sidebar = "Graphics";
    registry.AddSidebar(tab, sidebar, 2);
    const auto apply = [system] { system->ApplyRenderSettings(); };

    registry.AddWidget(tab, sidebar, "Display", WidgetType::Section);

    auto mode = registry.AddWidget(tab, sidebar, "Window Mode", WidgetType::Combo)
                    .Options({ "Windowed", "Fullscreen" })
                    .Tooltip("F11 also toggles this.");
    Widget* modeWidget = &mode.Get();
    mode.PreFunc([system](Widget& w) {
        w.intValue = system->IsFullscreen() ? 1 : 0;
        if (!system->HasWindow()) {
            w.disabled = true;
            w.disabledReason = "There is no window.";
        } else if (!system->CanChangeWindowMode()) {
            w.disabled = true;
            w.disabledReason = "This platform always runs fullscreen.";
        }
    });
    mode.Callback([system, modeWidget] { system->SetFullscreen(modeWidget->intValue == 1); });

    registry.AddWidget(tab, sidebar, "Borderless Fullscreen", WidgetType::Checkbox)
        .CVar(kWindowedFullscreenCVar)
        .Tooltip("Fullscreen at the desktop resolution in a borderless window, instead of taking over the display. "
                 "Applies the next time fullscreen is entered.")
        .PreFunc([system](Widget& w) {
            if (!system->SupportsWindowedFullscreen()) {
                w.disabled = true;
                w.disabledReason = "Only the OpenGL renderer supports this.";
            }
        });

    registry.AddWidget(tab, sidebar, "V-Sync", WidgetType::Checkbox)
        .CVar(kVsyncCVar)
        .Default(1)
        .Tooltip("Wait for the display's refresh before showing each frame.")
        .PreFunc([system](Widget& w) {
            if (!system->CanDisableVsync()) {
                w.disabled = true;
                w.disabledReason = "This platform always syncs to the display.";
            }
        });

    // The game's logic advances one tick per drawn frame, so a lower limit
    // would slow the game down rather than just draw less.
    // TODO(https://github.com/liz-baker/frigate/issues/112): a frame rate
    // limit, once the clock is decoupled from the frame rate.
    registry.AddWidget(tab, sidebar, "FPS Limit", WidgetType::SliderInt)
        .Range(20, 60, 5)
        .Default(60)
        .Disabled("The game's speed is tied to its frame rate, so it always runs at 60 FPS for now.");

    registry.AddWidget(tab, sidebar, "Rendering", WidgetType::Section).Column(1);

    registry.AddWidget(tab, sidebar, "Internal Resolution", WidgetType::SliderFloat)
        .Column(1)
        .CVar(kInternalResolutionCVar)
        .Range(0.5f, 4.0f, 0.25f)
        .Default(1.0f)
        .Format("%.2fx")
        .Tooltip("Scales the resolution the game is drawn at before it is fitted to the window.")
        .Callback(apply);

    registry.AddWidget(tab, sidebar, "Low-Resolution Mode", WidgetType::Checkbox)
        .Column(1)
        .CVar(kLowResModeCVar)
        .Tooltip("Draw at the console's original resolution.");

    registry.AddWidget(tab, sidebar, "Anti-Aliasing (MSAA)", WidgetType::SliderInt)
        .Column(1)
        .CVar(kMsaaCVar)
        .Range(1, 8, 1)
        .Default(1)
        .Format("%dx")
        .Tooltip("Multisampling level; 1x is off. The renderer caps it at what the GPU supports.")
        .Callback(apply)
        .PreFunc([system](Widget& w) {
            if (!system->SupportsMsaa()) {
                w.disabled = true;
                w.disabledReason = "This renderer doesn't support multisampling.";
            }
        });

    registry.AddWidget(tab, sidebar, "Texture Filtering", WidgetType::Combo)
        .Column(1)
        .CVar(kTextureFilterCVar)
        .Options({ "Three-Point", "Bilinear", "None" })
        .Default(0)
        .Tooltip("Three-point matches the console's filtering.")
        .Callback(apply);

    // The window backend is chosen when the window is created.
    auto backends = std::make_shared<std::vector<Choice>>(system->RenderBackends());
    auto renderer = registry.AddWidget(tab, sidebar, std::string("Renderer") + kRestartSuffix, WidgetType::Combo)
                        .Column(1)
                        .Options(ChoiceLabels(*backends))
                        .Tooltip("The graphics API the game draws with. Takes effect the next time the game starts.");
    Widget* rendererWidget = &renderer.Get();
    renderer.PreFunc([system, backends](Widget& w) {
        w.intValue = ChoiceIndex(*backends, system->RenderBackend());
        if (backends->size() < 2) {
            w.disabled = true;
            w.disabledReason = "No other renderer is available in this build.";
        }
    });
    renderer.Callback([system, backends, rendererWidget] {
        system->SetRenderBackend(ChoiceId(*backends, rendererWidget->intValue, system->RenderBackend()));
    });
}

} // namespace Frigate::ConfigMenu
