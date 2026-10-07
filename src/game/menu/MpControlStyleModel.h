#pragma once

#include <array>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer control style screen's state and logic (init_menu11_mpcontrol
// and interface_menu11_mpcontrols) apart from the drawing and the wallet: each
// player steps through MP_controller_configuration_table with left and right
// and locks a style in, which goes to FrontState::multiplayer.controlStyle.
//
// The one-controller styles (1.1 to 1.4) are every player's to choose. The
// two-controller styles (2.1 to 2.4) take a second pad, so only a two player
// game offers them, and then only the first player steps into them: the second
// player's pad is the first player's second pad, so the second player's style
// is 2.1 to 2.4 as well for as long as the first player's is.
class MpControlStyleModel {
  public:
    // MP_controller_configuration_table's rows: 1.1 to 1.4, then 2.1 to 2.4.
    static constexpr int kRows = 8;

    // init_menu11_mpcontrol (without the wallet): nobody has locked in.
    void Init(FrontState& front);

    // One frame of interface_menu11_mpcontrols after the view setup, with each
    // controller's reading in `inputs`. Moves `front.cursor` with the first
    // controller's stick, raises sounds into Sfx(), and returns whether every
    // player has locked in, which sends the game back to the options.
    bool Update(const std::array<Input::Sample, kMpMaxPlayers>& inputs, FrontState& front);

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    // The press of left (`previous`) or right for `player`.
    static void StepPrevious(std::array<Level::ControlConfig, kMpMaxPlayers>& styles, int player, int players);
    static void StepNext(std::array<Level::ControlConfig, kMpMaxPlayers>& styles, int player, int players);

    // dword_CODE_bss_80069760: whether the player's stick has been back near
    // the centre since it last stepped the style, so holding it steps once.
    std::array<bool, kMpMaxPlayers> stickReset_{};
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
