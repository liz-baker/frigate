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

// Registers MenuId::MultiplayerStageSelect's handlers: init_menu12_mpstage(),
// update_menu12_mpstage() (which does nothing), interface_menu12_mpstage() and
// constructor_menu12_mpstage(), a grid of the stages' previews on the open
// wallet. The logic is MpStageSelectModel; the choice lives in
// FrontState::multiplayer.stage.
//
// A null `resourceManager` registers the logic without the assets or drawing,
// which is how the menu flow is tested.
void RegisterMpStageSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front);

} // namespace Frigate::Game::Menu
