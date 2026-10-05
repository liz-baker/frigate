#pragma once

#include <functional>

namespace Frigate::ConfigMenu {

// Draws libultraship's input editor into the current ImGui window. Supplied
// by Install() because the editor needs libultraship, which the model library
// does not link; unset (as in tests, and on platforms without the editor),
// the tab says the editor isn't available.
void SetInputEditorDraw(std::function<void()> draw);

} // namespace Frigate::ConfigMenu
