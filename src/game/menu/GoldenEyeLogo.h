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

// Registers MenuId::GoldenEyeLogo's handlers: init_menu04_goldeneyelogo(),
// update_menu04_goldeneye(), interface_menu04_goldeneyelogo() and constructor_menu04_goldeneyelogo().
// PROP_GOLDENEYELOGO doesn't animate, so the model is built once and redrawn
// unchanged every frame.
void RegisterGoldenEyeLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
