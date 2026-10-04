#include "menu/Tabs.h"

#include "menu/tabs/SettingsTab.h"

namespace Frigate::ConfigMenu {

// One declaration per tab, each defined in its own file under tabs/.
void RegisterMenuTab(Registry& registry);

void RegisterTabs(Registry& registry, const std::shared_ptr<System>& system) {
    RegisterMenuTab(registry);
    RegisterSettingsTab(registry, system);
    // TODO(https://github.com/liz-baker/frigate/issues/251): controller configuration tab.
    // The Dev Tools tab (#355) is registered by Install(), after these.
}

} // namespace Frigate::ConfigMenu
