#pragma once

#include <memory>

#include "menu/MenuModel.h"
#include "menu/System.h"

namespace Frigate::ConfigMenu {

// The Settings tab: General, Audio and Graphics sidebars, one file each.
void RegisterSettingsTab(Registry& registry, const std::shared_ptr<System>& system);

void RegisterSettingsAudio(Registry& registry, const std::shared_ptr<System>& system);
void RegisterSettingsGraphics(Registry& registry, const std::shared_ptr<System>& system);

} // namespace Frigate::ConfigMenu
