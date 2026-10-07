#pragma once

#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer stage select's state and logic (init_menu12_mpstage and
// interface_menu12_mpstage) apart from the drawing and the wallet: which stage
// the cursor is over and the choice, which goes to FrontState::multiplayer.stage.
class MpStageSelectModel {
  public:
    // The grid of previews: kColumns across and kRows down, each cell a row of
    // the stage table (MpStage). The cursor lines at which the second and third
    // rows and each column begin.
    static constexpr int kColumns = 4;
    static constexpr int kRows = 3;
    static constexpr int32_t kSecondRowTop = 170;
    static constexpr int32_t kThirdRowTop = 240;
    static constexpr int32_t kColumnLeft[kColumns] = { 0, 122, 207, 292 };

    // multi_stage_setups' photo for each MpStage: the preview's index in
    // s_mpstageselimages (MPSELECTIMGIDS).
    static constexpr int kPhoto[kMpStageCount] = { 16, 12, 14, 15, 13, 13, 13, 4, 0, 2, 1, 10 };

    // init_menu12_mpstage (without the wallet): nothing chosen, and the first
    // preview, Random, highlighted.
    void Init(FrontState& front);

    // One frame of interface_menu12_mpstage after the view setup. Moves
    // `front.cursor`, raises sounds into Sfx(), and returns whether the screen is
    // done, which sends the game back to the options.
    bool Update(const Input::Sample& input, FrontState& front);

    // The cell the cursor is over, as a row of the stage table.
    static int CellAt(const MenuCursor& cursor);

    // current_mp_stage_highlighted.
    int Highlighted() const {
        return highlighted_;
    }
    bool TabPrevHighlighted() const {
        return tabPrevHighlight_;
    }
    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    int highlighted_ = 0;
    bool tabPrevHighlight_ = false;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
