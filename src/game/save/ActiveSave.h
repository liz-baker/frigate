#pragma once

#include <memory>

#include "game/save/SaveManager.h"

namespace Frigate::Game::Save {

// The save manager the running game uses, for tools outside the game loop
// (the menu's Dev Tools tab). Null until the game has built it.
std::shared_ptr<SaveManager> ActiveSaveManager();
void SetActiveSaveManager(const std::shared_ptr<SaveManager>& manager);

} // namespace Frigate::Game::Save
