#pragma once

namespace Frigate::ConfigMenu {

// libultraship's switch for gamepad navigation of its menus (its
// CVAR_IMGUI_CONTROLLER_NAV); with it off the gamepad neither toggles this
// menu nor drives it.
inline constexpr const char* kControllerNavCVar = "gControlNav";

// libultraship's switch for ImGui multi-viewport (its
// CVAR_ENABLE_MULTI_VIEWPORTS), on unless set: windows, tooltips and combo
// lists that reach past the game window get an OS window of their own.
inline constexpr const char* kMultiViewportsCVar = "gEnableMultiViewports";

// The panel's background opacity.
inline constexpr const char* kOpacityCVar = "gMenuOpacity";
inline constexpr float kDefaultOpacity = 0.85f;

// The menu's search box: in the sidebar column instead of the top bar, and
// focused each time the menu opens.
inline constexpr const char* kSearchInSidebarCVar = "gMenuSearchInSidebar";
inline constexpr const char* kSearchAutofocusCVar = "gMenuSearchAutofocus";

// libultraship's rendering CVars; the names must match the ones its CMake
// defines (cmake/cvars.cmake in the library), since it reads them itself.
inline constexpr const char* kVsyncCVar = "gVsyncEnabled";
inline constexpr const char* kInternalResolutionCVar = "gInternalResolution";
inline constexpr const char* kMsaaCVar = "gMSAAValue";
inline constexpr const char* kTextureFilterCVar = "gTextureFilter";
inline constexpr const char* kWindowedFullscreenCVar = "gSdlWindowedFullscreen";
inline constexpr const char* kLowResModeCVar = "gLowResMode";

} // namespace Frigate::ConfigMenu
