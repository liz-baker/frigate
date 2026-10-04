#include "menu/Tabs.h"

namespace Frigate::ConfigMenu {

// One declaration per tab, each defined in its own file under tabs/.
void RegisterMenuTab(Registry& registry);

void RegisterTabs(Registry& registry) {
    RegisterMenuTab(registry);
    // TODO(https://github.com/liz-baker/frigate/issues/251): controller configuration tab.
    // TODO(https://github.com/liz-baker/frigate/issues/356): settings tab.
    // TODO(https://github.com/liz-baker/frigate/issues/355): developer tools tab.
}

} // namespace Frigate::ConfigMenu
