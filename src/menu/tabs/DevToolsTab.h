#pragma once

#include <cstdint>
#include <functional>

#include "game/MultiplayerSettings.h"
#include "menu/MenuModel.h"

namespace Frigate::ConfigMenu {

// The Dev Tools tab: edits a copy of a folder's save and writes it back on
// Apply. `reset` restarts the game after Apply; `startMultiplayer` starts a
// multiplayer game without the menus. Separate from RegisterTabs() because it
// needs the game's save code.
void RegisterDevToolsTab(Registry& registry, std::function<void()> reset,
                         std::function<void(const Game::MultiplayerSettings&)> startMultiplayer = nullptr);

// A cheat's name, by id (Cheat::kExtraMpChars to Cheat::k2xLaser).
const char* CheatLabel(int32_t id);

// The tab's multiplayer sidebar (DevToolsMultiplayer.cpp). `start` runs with
// settings that can start a game.
void RegisterMultiplayerStart(Registry& registry, const std::function<void(const Game::MultiplayerSettings&)>& start);

} // namespace Frigate::ConfigMenu
