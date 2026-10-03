#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/save/SaveManager.h"

namespace Frigate::Game::Menu {

// The sound effects the mission select plays (sndPlaySfx).
enum class MissionSelectSfx {
    DoorMetalClose2,
};

// What the mission select asks of the current folder's progress.
// SaveProgress backs it in the game (MissionSelect.cpp).
class MissionSelectProgress {
  public:
    virtual ~MissionSelectProgress() = default;
    // fileIs007ModeUnlocked of the current folder.
    virtual bool Is007ModeUnlocked() const = 0;
    // fileIsStageUnlockedAtDifficulty of the current folder.
    virtual Save::StageStatus StatusAt(int32_t level, int32_t difficulty) const = 0;
    // g_AppendCheatSinglePlayer: the screen is listing missions for the cheat
    // menu, where only completed stages count.
    virtual bool CheatVariant() const = 0;
};

// A vertex colour of the mission icons' picture list.
struct IconColour {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

// The mission select's logic (init_menu07_missionselect and
// interface_menu07_missionsel), apart from the drawing and the wallet model:
// which mission the cursor is over, locked and unlocked stages, and where the
// screen goes next.
class MissionSelectModel {
  public:
    static constexpr int32_t kColumns = 5;
    static constexpr int32_t kRows = 4;
    static constexpr int32_t kMissionCount = kColumns * kRows;

    // cursor_xpos_table_mission_select and cursor_ypos_table_mission_select
    // (NTSC): where each column and row of icons is.
    static constexpr std::array<int32_t, kColumns> kColumnX = { 73, 142, 212, 282, 352 };
    static constexpr std::array<int32_t, kRows> kRowY = { 62, 131, 201, 270 };

    enum class Next {
        Stay,
        Difficulty,
        ModeSelect,
    };

    // init_menu07_missionselect: nothing is selected.
    void Init();

    // get_highest_unlocked_difficulty_for_level: the highest difficulty the
    // stage is playable at, or -1 when it is locked (or not a mission).
    static int32_t HighestUnlockedDifficulty(int32_t level, const MissionSelectProgress& progress);

    // The colour interface_menu07_missionsel gives the four vertices of a
    // mission's icon.
    static IconColour IconVertexColour(int32_t level, int32_t highlighted, const MissionSelectProgress& progress);

    // One frame of interface_menu07_missionsel, from the cursor's position to
    // the screen's change. Moves `cursor`, raises sounds into Sfx() and
    // returns where the screen goes next.
    Next Update(const Input::Sample& input, MenuCursor& cursor, const MissionSelectProgress& progress);

    // mission_difficulty_highlighted: the mission (a LEVEL_SOLO_SEQUENCE value)
    // under the cursor, or -1 over the Previous tab.
    int32_t Highlighted() const {
        return highlighted_;
    }
    // tab_prev_highlight.
    bool PreviousTabHighlighted() const {
        return prevTabHighlighted_;
    }
    // selected_stage and briefingpage, as chosen: the stage's LevelId value and
    // its row of mission_folder_setup_entries.
    int32_t SelectedStage() const {
        return selectedStage_;
    }
    int32_t BriefingPage() const {
        return briefingPage_;
    }
    bool TabNextSelected() const {
        return tabNextSelected_;
    }
    bool TabPrevSelected() const {
        return tabPrevSelected_;
    }
    const std::vector<MissionSelectSfx>& Sfx() const {
        return sfx_;
    }

    // set_cursor_to_stage_solo: the cursor onto a stage's icon.
    static void PlaceCursorOnStage(int32_t level, MenuCursor& cursor);

    // pull_and_display_text_for_folder_a0: the row of mission_folder_setup_entries
    // whose stage is `level`, or -1.
    static int32_t EntryForLevel(int32_t level);

  private:
    int32_t FindHighlighted(const MenuCursor& cursor, const MissionSelectProgress& progress) const;

    int32_t highlighted_ = -1;
    bool prevTabHighlighted_ = false;
    int32_t selectedStage_ = -1;
    int32_t briefingPage_ = -1;
    bool tabNextSelected_ = false;
    bool tabPrevSelected_ = false;
    std::vector<MissionSelectSfx> sfx_;
};

// set_cursor_pos_difficulty: where the difficulty screen's cursor starts for a
// stage whose highest unlocked difficulty is `difficulty`.
inline void SetCursorForDifficulty(MenuCursor& cursor, int32_t difficulty) {
    cursor.h = 106.0f;
    cursor.v = static_cast<float>((difficulty * 0x1e) + 0xba);
}

} // namespace Frigate::Game::Menu
