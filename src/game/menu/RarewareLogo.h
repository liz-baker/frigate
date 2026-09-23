#pragma once

#include <memory>

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
struct IntroState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// Registers MenuId::RarewareLogo's handlers: init_menu02_rarelogo(),
// interface_menu02_rareware() (minus controller input, #40) and
// constructor_menu02_rareware()'s spinning, fading logo. The screen hands
// off to the gun barrel by setting IntroState::gunbarrelMode.
void RegisterRarewareLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                          const std::shared_ptr<FrontState>& front, const std::shared_ptr<IntroState>& intro);

} // namespace Frigate::Game::Menu
