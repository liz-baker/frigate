#pragma once

namespace Frigate::ConfigMenu {

// libultraship's switch for gamepad navigation of its menus (its
// CVAR_IMGUI_CONTROLLER_NAV); with it off the gamepad neither toggles this
// menu nor drives it.
inline constexpr const char* kControllerNavCVar = "gControlNav";

// The panel's background opacity.
inline constexpr const char* kOpacityCVar = "gMenuOpacity";
inline constexpr float kDefaultOpacity = 0.85f;

} // namespace Frigate::ConfigMenu
