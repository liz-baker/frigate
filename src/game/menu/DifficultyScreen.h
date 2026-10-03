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

// Registers MenuId::Difficulty's handlers (init_menu08_difficultyselect(),
// interface_menu08_difficulty() and constructor_menu08_difficulty(), the
// difficulties the stage is unlocked on) and MenuId::Options007's
// (init_menu09_007difficultyselect(), interface_menu09_007options() and
// constructor_menu09_007options(), the 007 sliders). The logic is
// DifficultyModel and Options007Model. A difficulty continues to
// MenuId::Briefing, or on 007 to the sliders and then the briefing.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterDifficulty(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
