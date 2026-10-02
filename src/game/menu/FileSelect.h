#pragma once

#include <memory>

namespace Frigate::Game {
class MenuStateMachine;
struct FrontState;
} // namespace Frigate::Game

namespace Frigate::Game::Menu {

// Registers MenuId::FileSelect's handlers: only init_menu05_fileselect()'s
// reset and the idle timeout of interface_menu05_fileselect() that sends the
// front-end back to the legal screen, which restarts the title cycle.
// TODO(https://github.com/liz-baker/frigate/issues/95): the folders, their
// cursor and the rest of the screen; until then it draws only the black clear.
void RegisterFileSelect(MenuStateMachine& menu, const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
