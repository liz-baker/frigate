#pragma once

#include <functional>

#include "menu/MenuModel.h"

namespace Frigate::ConfigMenu {

// The Dev Tools tab: edits a copy of a folder's save and writes it back on
// Apply. Hidden while `enabled` returns false. `reset` restarts the game after Apply. Separate from RegisterTabs()
// because it needs the game's save code.
void RegisterDevToolsTab(Registry& registry, std::function<bool()> enabled, std::function<void()> reset);

} // namespace Frigate::ConfigMenu
