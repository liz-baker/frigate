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

// Registers MenuId::EyeIntro's handlers: init_menu03_gunbarrel(),
// update_menu_03_gunbarrel(), interface_menu03_eye() and constructor_menu03_eye(), which is
// renderGunbarrelEyeIntroSequence(): the barrel sweep, Bond walking across
// the sight and firing, the blood and the fade to black.
void RegisterEyeIntro(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                      const std::shared_ptr<FrontState>& front, const std::shared_ptr<IntroState>& intro);

} // namespace Frigate::Game::Menu
