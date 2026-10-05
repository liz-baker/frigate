#pragma once

#include <memory>

#include "menu/System.h"

namespace Ship {
class Context;
}

namespace Frigate::ConfigMenu {

// The System backed by the running libultraship Context.
std::shared_ptr<System> MakeSystem(const std::shared_ptr<Ship::Context>& context);

} // namespace Frigate::ConfigMenu
