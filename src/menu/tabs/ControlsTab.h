#pragma once

#include <functional>
#include <string>

namespace Frigate::ConfigMenu {

// Draws the port tabs' binding editor into the current ImGui window. Supplied
// by Install() because the editor needs ImGui, which the model library does not
// link; unset (as in tests), the tab says the editor isn't available.
using ControlsEditorDrawFn = std::function<void()>;
void SetControlsEditorDraw(ControlsEditorDrawFn draw);

// How many players the connected ports make, shown in each port's tab.
std::string PlayerCountText(int players);

} // namespace Frigate::ConfigMenu
