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

// Registers MenuId::FileSelect's handlers: init_menu05_fileselect(),
// update_menu05_filesel(), interface_menu05_fileselect() and
// constructor_menu05_fileselect(), the four folders with the crosshair cursor,
// the copy and erase icons and the erase prompt. The logic is FileSelectModel.
// Choosing a folder continues to MenuId::ModeSelect.
void RegisterFileSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
