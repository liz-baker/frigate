#pragma once

#include <memory>

namespace Ship {
class Context;
}

namespace Frigate::ConfigMenu {

// Builds the menu from RegisterTabs() and installs it on the window's Gui,
// hidden. Does nothing when the context has no window. Also defaults
// libultraship's controller-navigation CVar on so the gamepad can open the
// menu; a value the player has set is kept.
void Install(const std::shared_ptr<Ship::Context>& context);

} // namespace Frigate::ConfigMenu
