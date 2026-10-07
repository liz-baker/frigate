#pragma once

#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer team select's state and logic (init_menu14_mpteamsel and
// interface_menu14_mpteams) apart from the drawing and the wallet. The first
// controller moves the player who differs through the 2 by 2 grid of player
// panels, `teamSize` in FrontState::multiplayer: in 2v2 it is the first
// player's partner, in 3v1 and 2v1 the player alone on the second team. A
// press of A, Z or Start accepts it; MultiplayerSettings::TeamFlag gives the
// flag set_players_team_or_scenario_item_flag stores for each player from it.
class MpTeamsModel {
  public:
    // How many frames a flashing panel's text is shown and hidden in
    // (D_8002B560 counts to twice this).
    static constexpr int kFlashFrames = 10;

    // init_menu14_mpteamsel (without the wallet): the partner or loner starts
    // at the last player, or the third for 2v1.
    void Init(FrontState& front);

    // One frame of interface_menu14_mpteams after the view setup, with the
    // first controller's reading. Moves `front.cursor`, raises sounds into
    // Sfx(), and returns whether the teams were accepted, which sends the game
    // back to the options.
    bool Update(const Input::Sample& input, FrontState& front);

    // Whether the panel of `player` shows its team text this frame: the
    // player who differs flashes.
    bool TextShown(const FrontState& front, int player) const;

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    // D_8002B560.
    int flash_ = 0;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
