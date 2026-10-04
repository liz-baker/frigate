#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "game/MenuId.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// interface_menu00_legalscreen()'s choice of the next screen, or nothing to
// stay. The screen times out to the Nintendo logo (the no-controllers screen
// on the first pass with none connected); once it has been seen, any button
// skips ahead to the file select, or to the Nintendo logo while the title
// sequence has not yet played through.
std::optional<MenuId> NextAfterLegalScreen(int32_t menuTimer, bool anyButtonPressed, int controllerCount,
                                           const FrontState& front);

// Registers MenuId::LegalScreen's handlers: interface_menu00_legalscreen(),
// and init_menu00_legalscreen()/
// constructor_menu00_legalscreen()'s model instancing and drawing.
// PROP_LEGALPAGE has no animation, so the model is built once and redrawn
// unchanged every frame; the LTITLE strings are drawn over it. init_menu00_
// legalscreen also runs fileValidateSaves(), which `front->save` stands in for.
void RegisterLegalScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                         const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
