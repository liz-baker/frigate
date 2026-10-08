#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MultiplayerOptions.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The multiplayer options screen's state and logic (init_menu0E_mpoptions and
// interface_menu0E_mpoptions) apart from the drawing and the wallet: which row
// the cursor is over, and what a press does to FrontState::multiplayer or where
// it goes. The settings are the ones the other multiplayer screens and the stage
// share.
class MpOptionsModel {
  public:
    // The screens a frame can ask to go to, in the order frontChangeMenu is
    // called.
    enum class Next {
        ModeSelect,
        RunStage,
        ScenarioSelect,
        StageSelect,
        CharacterSelect,
        Handicap,
        ControlStyle,
    };

    // The rows, which are also the highlight_* flags: at most one is set.
    enum class Row {
        Players,
        Scenario,
        Stage,
        GameLength,
        Weapons,
        Character,
        Health,
        ControlStyle,
        Aim,
    };

    // The cursor rows each row starts at (the thresholds interface_menu0E_mpoptions
    // compares cursor_v_pos with); anything above the scenario's is the player
    // count.
    static constexpr int32_t kScenarioRow = 0x8D;
    static constexpr int32_t kStageRow = 0xA1;
    static constexpr int32_t kGameLengthRow = 0xB5;
    static constexpr int32_t kWeaponsRow = 0xC9;
    static constexpr int32_t kCharacterRow = 0xDD;
    static constexpr int32_t kHealthRow = 0xF1;
    static constexpr int32_t kControlStyleRow = 0x105;
    static constexpr int32_t kAimRow = 0x119;

    // init_menu0E_mpoptions (without the music and the wallet): nothing chosen or
    // highlighted. The first visit takes the player count from the connected
    // controllers (`controllerCount`, joyGetControllerCount); a stage the folder
    // has not unlocked is replaced by Temple.
    void Init(FrontState& front, int controllerCount);

    // One frame of interface_menu0E_mpoptions after the view setup. `nextRandom`
    // is randomGetNext, drawn from when Random stage is chosen at Start. Moves
    // `front.cursor`, raises sounds into Sfx() and returns the screens it asks
    // for, last request last.
    const std::vector<Next>& Update(const Input::Sample& input, FrontState& front, int controllerCount,
                                    const std::function<uint32_t()>& nextRandom);

    std::optional<Row> HighlightedRow() const {
        return row_;
    }
    bool TabPrevHighlighted() const {
        return tabPrevHighlight_;
    }
    bool TabStartHighlighted() const {
        return tabStartHighlight_;
    }
    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    void UpdateHighlights(const FrontState& front);

    bool tabStartSelected_ = false;
    bool tabPrevHighlight_ = false;
    bool tabStartHighlight_ = false;
    std::optional<Row> row_;
    std::vector<FolderSfx> sfx_;
    std::vector<Next> next_;
};

} // namespace Frigate::Game::Menu
