#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// Registers MenuId::DisplayCast's handlers: init_menu18_displaycast(),
// update_menu18_displaycast(), interface_menu18_displaycast() and constructor_menu18_displaycast(). One cast
// member per visit: a randomly animated character, armed with a random
// weapon if the animation calls for one, seen from a camera that drifts
// around them and trails their movement, with three lines of text.
void RegisterCastScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
