#pragma once

#include <array>
#include <functional>
#include <string>

namespace Frigate::ConfigMenu {

// Draws libultraship's input editor into the current ImGui window. Supplied
// by Install() because the editor needs libultraship, which the model library
// does not link; unset (as in tests, and on platforms without the editor),
// the tab says the editor isn't available. The argument holds, per port, why
// its bindings can't be edited (its device is disconnected), empty when they can.
using InputEditorDrawFn = std::function<void(const std::array<std::string, 4>& disabledReasons)>;
void SetInputEditorDraw(InputEditorDrawFn draw);

// The line under the port checkboxes: how many players the connected ports make.
std::string PlayerCountText(int players);

} // namespace Frigate::ConfigMenu
