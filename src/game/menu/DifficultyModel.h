#pragma once

#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"
#include "game/save/SaveProgress.h"

namespace Frigate::Game::Menu {

// The selected difficulty (selected_difficulty), which SaveProgress backs in
// the game.
class DifficultySelection {
  public:
    virtual ~DifficultySelection() = default;
    virtual int32_t Get() const = 0;
    virtual void Set(int32_t difficulty) = 0;
};

// The difficulty screen's state and logic (init_menu08_difficultyselect and
// interface_menu08_difficulty) apart from the drawing and the wallet model:
// which difficulty the cursor is over, choosing one and the Previous tab.
class DifficultyModel {
  public:
    enum class Next {
        Stay,
        Options007,
        Briefing,
        MissionSelect,
    };

    // init_menu08_difficultyselect: no difficulty chosen, no tab chosen.
    void Init(DifficultySelection& selection, bool& tabNextSelected, bool& tabPrevSelected);

    // One frame of interface_menu08_difficulty. `unlocked` is
    // HighestUnlockedDifficultyForLevel for the briefing page's level and
    // `level` that level. Moves `cursor` (the stick, and onto the next tab or
    // the stage's icon as the screen is left) and raises sounds into Sfx().
    Next Update(const Input::Sample& input, int32_t unlocked, int32_t level, MenuCursor& cursor,
                DifficultySelection& selection, bool& tabNextSelected, bool& tabPrevSelected);

    // mission_difficulty_highlighted: the difficulty the cursor is over, or -1.
    int32_t Highlighted() const {
        return highlighted_;
    }
    // tab_prev_highlight.
    bool PreviousTabHighlighted() const {
        return tabPrevHighlight_;
    }

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    int32_t highlighted_ = Save::kDifficultyMulti;
    bool tabPrevHighlight_ = false;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
