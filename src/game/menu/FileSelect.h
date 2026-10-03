#pragma once

#include <array>
#include <memory>

#include "game/menu/FileSelectModel.h"

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
// Choosing a folder continues to MenuId::ModeSelect. A null `resourceManager`
// registers the logic without the assets or drawing, which is how the menu flow
// is tested.
void RegisterFileSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front);

// Replaces the folders' hit boxes, which are otherwise projected from the
// loaded wallet models, for tests that run without them.
void SetFolderBoundsForTesting(const std::array<ScreenRect, 4>& bounds);

} // namespace Frigate::Game::Menu
