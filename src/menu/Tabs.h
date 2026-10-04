#pragma once

#include "menu/MenuModel.h"

namespace Frigate::ConfigMenu {

// The one place every tab is registered; tabs appear in the order listed
// here. To add a tab, add a Register<Name>Tab(Registry&) in its own file under
// src/menu/tabs/ and one line to this function (docs/menu.md).
void RegisterTabs(Registry& registry);

} // namespace Frigate::ConfigMenu
