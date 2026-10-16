#pragma once

// The Settings tab's logic that doesn't touch ImGui or libultraship.

#include <array>
#include <string>
#include <vector>

#include "menu/System.h"

namespace Frigate::ConfigMenu {

// The menu's colour themes, in the order of the palettes in MenuWindow.cpp.
inline constexpr std::array<const char*, 3> kMenuThemes = { "Blue", "Graphite", "Crimson" };

// Combo-box labels for a list of choices, in order.
std::vector<std::string> ChoiceLabels(const std::vector<Choice>& choices);

// Position of the choice with `id`; 0 when absent so a combo always has a
// valid selection.
int ChoiceIndex(const std::vector<Choice>& choices, int id);

// The id at `index`, or `fallback` when out of range.
int ChoiceId(const std::vector<Choice>& choices, int index, int fallback);

// A file:// URL for an absolute filesystem path (POSIX or Windows style), with
// anything outside the URL-safe set percent-encoded.
std::string FileUrlFromPath(const std::string& path);

// The version line for the About column.
std::string VersionLabel(const std::string& describe);

// Suffix on the name of a setting that only applies after a relaunch.
inline constexpr const char* kRestartSuffix = " (restart required)";

// Suffix on the name of a setting that applies when the game is reset (the menu's reset button) or relaunched.
inline constexpr const char* kResetSuffix = " (applies on reset)";

} // namespace Frigate::ConfigMenu
