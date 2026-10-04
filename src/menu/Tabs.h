#pragma once

#include <memory>

#include "menu/MenuModel.h"
#include "menu/System.h"

namespace Frigate::ConfigMenu {

// The one place every tab is registered; tabs appear in the order listed
// here. To add a tab, add a Register<Name>Tab(Registry&) in its own file under
// src/menu/tabs/ and one line to this function (docs/menu.md). `system` is what
// tabs that touch the window, audio or host OS go through.
void RegisterTabs(Registry& registry, const std::shared_ptr<System>& system);

} // namespace Frigate::ConfigMenu
