#pragma once

#include <array>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer handicap screen's state and logic (init_menu10_mphandicap
// and interface_menu10_mphandicap) apart from the drawing and the wallet: each
// player steps their own row of MP_handicap_table with left and right and
// locks it in, and the choice goes to FrontState::multiplayer.handicap.
class MpHandicapModel {
  public:
    // The last row of MP_handicap_table; 0 is the hardest handicap.
    static constexpr int kLastRow = MultiplayerSettings::kHandicapRows - 1;

    // init_menu10_mphandicap (without the wallet): nobody has locked in.
    void Init(FrontState& front);

    // One frame of interface_menu10_mphandicap after the view setup, with each
    // controller's reading in `inputs`. Moves `front.cursor` with the first
    // controller's stick, raises sounds into Sfx(), and returns whether every
    // player has locked in, which sends the game back to the options.
    bool Update(const std::array<Input::Sample, kMpMaxPlayers>& inputs, FrontState& front);

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    // dword_CODE_bss_80069760: whether the player's stick has been back near
    // the centre since it last stepped the row, so holding it steps once.
    std::array<bool, kMpMaxPlayers> stickReset_{};
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
