#pragma once

#include <functional>
#include <memory>

#include "game/MultiplayerSettings.h"

namespace Ship {
class Context;
}

namespace Frigate::ConfigMenu {

// Builds the menu from RegisterTabs() and installs it on the window's Gui,
// hidden. Does nothing when the context has no window. Also defaults
// libultraship's controller-navigation CVar on so the gamepad can open the
// menu; a value the player has set is kept. `reset` backs the menu's reset
// button, which stays disabled without it. `startMultiplayer` backs the Dev
// Tools tab's multiplayer game.
void Install(const std::shared_ptr<Ship::Context>& context, std::function<void()> reset = nullptr,
             std::function<void(const Game::MultiplayerSettings&)> startMultiplayer = nullptr);

} // namespace Frigate::ConfigMenu
