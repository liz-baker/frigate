#pragma once

#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer scenario select's state and logic (init_menu13_mpscenariosel
// and interface_menu13_mpscenario) apart from the drawing and the wallet: which
// scenario the cursor is over and the choice, which goes to
// FrontState::multiplayer through MpOptions::ResetForScenario.
class MpScenarioSelectModel {
  public:
    // Where the screen goes after a frame.
    enum class Next {
        None,
        Options,
        Teams,
    };

    // The cursor line at which the first scenario's row starts, and the rows' spacing.
    static constexpr int32_t kFirstRowTop = 0x83;
    static constexpr int32_t kRowSpacing = 0x16;

    // init_menu13_mpscenariosel (without the wallet): nothing chosen. The
    // highlighted scenario is kept from the last visit.
    void Init(FrontState& front);

    // One frame of interface_menu13_mpscenario after the view setup. Moves
    // `front.cursor`, raises sounds into Sfx() and says where to go.
    Next Update(const Input::Sample& input, FrontState& front);

    // Whether `scenario` fits `playerCount` players (mp_player_counts' range).
    static bool Fits(MpScenario scenario, int playerCount);

    // The scenario highlighted, as dword_80069780 holds it: the scenario's
    // number plus one, 0 while the previous tab is.
    int32_t Highlighted() const {
        return highlighted_;
    }
    bool TabPrevHighlighted() const {
        return tabPrevHighlight_;
    }
    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    int32_t highlighted_ = 0;
    bool tabPrevHighlight_ = false;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
