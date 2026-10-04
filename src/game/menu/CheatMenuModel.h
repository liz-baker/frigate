#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The cheat screen's logic (init_menu15_cheat, update_menu15_cheat and
// interface_menu15_cheat) apart from the drawing: the list of unlocked
// cheats, the cursor's row highlight, switching a cheat on or off and the
// previous tab back to the mode select. The state it works on is FrontState's
// (g_CheatActivated, the cursor and the tab flags).
class CheatMenuModel {
  public:
    enum class Next {
        Stay,
        ModeSelect,
    };

    // Rows in the left column; a longer list continues in the right one.
    static constexpr int32_t kColumnRows = 12;
    // The right column takes the cursor from this x.
    static constexpr int32_t kSecondColumnCursorX = 0xDC;
    static constexpr int32_t kRowTop = 0x35;
    static constexpr int32_t kRowHeight = 0x14;

    // init_menu15_cheat: clears the tab flags and lists the available cheats
    // in id order. The highlighted row is kept from the last visit, as the
    // global it is in the original is.
    void Init(FrontState& front);

    // One frame of interface_menu15_cheat. Raises sounds into Sfx().
    Next Update(const Input::Sample& input, FrontState& front);

    // update_menu15_cheat: the single-player and multiplayer screens' cheat
    // variants follow whether any cheat is on, and whether any such cheat
    // applies in multiplayer.
    static void UpdateAppendFlags(FrontState& front);

    // totalunlockedcheats.
    int32_t TotalUnlocked() const {
        return totalUnlocked_;
    }
    // arrayUnlockedCheats[index].
    int32_t UnlockedCheat(int32_t index) const {
        return unlocked_[static_cast<size_t>(index)];
    }
    // cheathighlighted.
    int32_t Highlighted() const {
        return highlighted_;
    }
    // tab_prev_highlight.
    bool PreviousTabHighlighted() const {
        return previousTabHighlighted_;
    }

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    void HighlightRowAtCursor(const MenuCursor& cursor);

    int32_t totalUnlocked_ = 0;
    std::array<int32_t, kCheatInvalid> unlocked_{};
    int32_t highlighted_ = 0;
    bool previousTabHighlighted_ = false;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
