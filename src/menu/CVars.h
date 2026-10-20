#pragma once

#include <array>

namespace Frigate::ConfigMenu {

// libultraship's switch for gamepad navigation of its menus (its
// CVAR_IMGUI_CONTROLLER_NAV); with it off the gamepad neither toggles this
// menu nor drives it.
inline constexpr const char* kControllerNavCVar = "gControlNav";

// libultraship's switch for ImGui multi-viewport (its
// CVAR_ENABLE_MULTI_VIEWPORTS), on unless set: windows, tooltips and combo
// lists that reach past the game window get an OS window of their own.
inline constexpr const char* kMultiViewportsCVar = "gEnableMultiViewports";

// The panel's colour theme (an index into kMenuThemes) and background opacity.
inline constexpr const char* kThemeCVar = "gMenuTheme";
inline constexpr const char* kOpacityCVar = "gMenuOpacity";
inline constexpr float kDefaultOpacity = 0.85f;

// The master volume, in percent, applied to the game's audio output.
inline constexpr const char* kMasterVolumeCVar = "gMasterVolume";

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

// Whether texture requests use their mip chains (packed as `.mip` next to the image) or load the base
// level alone, as before chains. Read each time a display list is expanded; the game's reset drops the
// cached lists, so it takes effect on reset or the next start. On unless set.
inline constexpr const char* kMipChainsCVar = "gMipChains";

// The display's aspect ratio: 0 fits the window, 1 is 4:3, 2 is 16:9. Drives
// libultraship's gAdvancedResolution.* CVars (see System::ApplyRenderSettings).
inline constexpr const char* kAspectRatioCVar = "gDisplayAspect";

// Per controller port (1 to 4): whether it counts as connected with nothing
// attached.
inline constexpr std::array<const char*, 4> kPortConnectedCVars = { "gPort1Connected", "gPort2Connected",
                                                                    "gPort3Connected", "gPort4Connected" };

// Per controller port (1 to 4): the device that feeds it, as the string
// Platform::Input::DeviceKey writes. Unset reads as the port's default bindings.
inline constexpr std::array<const char*, 4> kPortDeviceCVars = { "gPort1DeviceKey", "gPort2DeviceKey",
                                                                 "gPort3DeviceKey", "gPort4DeviceKey" };

// Per controller port (1 to 4): whether Modern mode's Crouch toggles (set) or is
// held (unset).
inline constexpr std::array<const char*, 4> kPortCrouchToggleCVars = { "gPort1CrouchToggle", "gPort2CrouchToggle",
                                                                       "gPort3CrouchToggle", "gPort4CrouchToggle" };

// Per controller port (1 to 4): Modern mode's mouse sensitivity and the right
// stick's look sensitivity, in percent (100 when unset).
inline constexpr std::array<const char*, 4> kPortMouseSensitivityCVars = {
    "gPort1MouseSensitivity", "gPort2MouseSensitivity", "gPort3MouseSensitivity", "gPort4MouseSensitivity"
};
inline constexpr std::array<const char*, 4> kPortLookSensitivityCVars = {
    "gPort1LookSensitivity", "gPort2LookSensitivity", "gPort3LookSensitivity", "gPort4LookSensitivity"
};
// 100% is kMouseDegreesPerPixel and the decomp's full-stick look speed.
inline constexpr int kDefaultMouseSensitivityPercent = 150;
inline constexpr int kDefaultLookSensitivityPercent = 100;
inline constexpr int kMinSensitivityPercent = 0;
inline constexpr int kMaxSensitivityPercent = 200;

} // namespace Frigate::ConfigMenu
