#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"

namespace Frigate::Game::Menu {

// The sound effects the mode select plays (sndPlaySfx), in the order they are
// raised.
enum class ModeSelectSfx {
    DoorMetalClose,
    DoorMetalClose2,
};

// The mode select's choice list and tab (init_menu06_modeselect and
// interface_menu06_modesel), apart from the drawing and the wallet: which entry
// the cursor is over, which are open, and where a press goes.
class ModeSelectModel {
  public:
    enum class Next {
        Stay,
        MissionSelect,
        MultiplayerOptions,
        Cheat,
        FileSelect,
    };

    // mission_difficulty_highlighted as this screen uses it: the entry the
    // constructor draws highlighted, or none (DIFFICULTY_MULTI) with the
    // cursor on the tab.
    enum class Highlight : int32_t {
        None = -1,
        SinglePlayer = 0,
        Multiplayer = 1,
        Cheats = 2,
    };

    // The cursor rows the entries start at: the cheat entry from 275, the
    // multiplayer entry from 243.
    static constexpr float kCheatRow = 275.0f;
    static constexpr float kMultiplayerRow = 243.0f;

    // init_menu06_modeselect (without the wallet): no choice made, no tab
    // selected.
    void Init(FrontState& front);

    // One frame of interface_menu06_modesel after the view setup and the wallet
    // pose. `controllerCount` is joyGetControllerCount and `cheatUnlocked`
    // frontCheckIfCheatIsUnlocked. Moves `front.cursor`, updates
    // `front.cheatAvailable` and `front.gameMode`, raises sounds into Sfx() and
    // returns where the screen goes next.
    Next Update(const Input::Sample& input, FrontState& front, int controllerCount,
                const std::function<bool(int32_t cheat)>& cheatUnlocked);

    // is_cheat_menu_available.
    bool CheatMenuAvailable() const {
        return cheatMenuAvailable_;
    }
    Highlight CurrentHighlight() const {
        return highlight_;
    }
    // tab_prev_highlight.
    bool PreviousTabHighlighted() const {
        return tabPrevHighlight_;
    }
    // The sounds raised by the last Update.
    const std::vector<ModeSelectSfx>& Sfx() const {
        return sfx_;
    }

  private:
    bool cheatMenuAvailable_ = false;
    Highlight highlight_ = Highlight::None;
    bool tabPrevHighlight_ = false;
    std::vector<ModeSelectSfx> sfx_;
};

} // namespace Frigate::Game::Menu
