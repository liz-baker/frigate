#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "game/MultiplayerSettings.h"

namespace Frigate::Game {

namespace Save {
class SaveProgress;
}

// None of the multiplayer front-end's settings (player_char, player_handicap,
// controlstyle_player, scenario, game_length, mp_weapon_set, teamsize,
// aim_sight_adjustment, MP_stage_selected, selected_num_players) is in the save
// file: they are plain globals that carry from one game to the next for as
// long as the process runs, and a fresh run starts from their initial values.
// What the save does decide is below: which stages and characters are open.
constexpr int kMpStageRandom = static_cast<int>(MpStage::Random);
constexpr int kMpStageTemple = static_cast<int>(MpStage::Temple);
constexpr int kMpStageCount = static_cast<int>(MpStage::Count);

// num_chars_selectable_mp's values: the eight Bond characters on a fresh save,
// 0x21 once the folder has completed Cradle, all 0x40 rows with the Extra
// multiplayer characters cheat.
constexpr int kMpCharsDefault = 8;
constexpr int kMpCharsAfterCradle = 0x21;
constexpr int kMpCharsAll = 0x40;

// get_selected_num_players: selected_num_players in multiplayer mode and 1 in
// any other.
int SelectedPlayerCount(bool multiplayerMode, int selectedNumPlayers);

// get_players_who_have_selected_mp_char: whether one of the first
// `playerCount` players has locked in `character`.
bool PlayersHaveSelectedChar(int character, int playerCount, const std::array<int, kMpMaxPlayers>& playerChar,
                             const std::array<bool, kMpMaxPlayers>& hasSelected);

// What a multiplayer game offers that the player's save progress opens up:
// stages and characters. The stage queries read the current folder of the
// SaveProgress passed in (selected_folder_num). The character count is
// front.c's num_chars_selectable_mp, which lives for the whole run and is only
// raised again by the cheat.
class MultiplayerUnlocks {
  public:
    // multi_stage_setups[stage].unlock_after: the solo level a folder must
    // have unlocked at Agent, -1 for a stage that is always open (and for one
    // outside the table).
    static int UnlockAfter(int stage);

    // check_if_mp_stage_unlocked: the stage has players to offer for
    // `playerCount` (get_selected_num_players) and, for the stages beyond the
    // first seven, the folder has unlocked the matching solo level at Agent.
    // Out-of-range stages are locked where the decomp reads past its table.
    static bool IsStageUnlocked(const Save::SaveProgress& progress, int stage, int playerCount);

    // init_menu0E_mpoptions: Temple stands in for a selected stage that is
    // locked. init_mp_options_for_scenario: and for one with room for fewer
    // players than `playerCount`.
    static int StageWhenOpeningOptions(const Save::SaveProgress& progress, int selectedStage, int playerCount);
    static int StageForPlayerCount(int selectedStage, int playerCount);

    // The stage select cursor's fallback: while the highlighted stage is
    // locked the highlight steps back a row of the table (Random, row 0, is
    // always open).
    static int HighlightFallback(const Save::SaveProgress& progress, int highlighted, int playerCount);

    // The Random choice at game start: stages 1 to 11 are drawn with
    // `nextRandom` (randomGetNext) modulo 11 until one is unlocked. Returns
    // the drawn stage's row.
    static int ResolveRandomStage(const Save::SaveProgress& progress, int playerCount,
                                  const std::function<uint32_t()>& nextRandom);

    // Whether each stage row is unlocked, for drawing the stage select.
    static std::array<bool, kMpStageCount> UnlockedStages(const Save::SaveProgress& progress, int playerCount);

    // num_chars_selectable_mp: how many mp_chr_setup rows the character select
    // scrolls through; rows from this index on are not selectable.
    int NumCharsSelectable() const {
        return numChars_;
    }
    bool IsCharacterSelectable(int row) const {
        return row >= 0 && row < numChars_;
    }
    // unlock_all_mp_chars, run when the Extra multiplayer characters cheat is
    // switched on.
    void UnlockAllCharacters() {
        numChars_ = kMpCharsAll;
    }

    // init_menu0f_mpcharsel: unless the cheat opened every character, the
    // count follows whether the current folder has completed Cradle at Agent.
    // When it falls back to the first eight, a player whose character is
    // beyond them is moved to the first of those that no player has locked in
    // (selected_num_players is `playerCount`; `hasSelected` is as the previous
    // visit left it). Every player slot is checked, including those beyond
    // `playerCount`, and a negative character is kept.
    void OnCharacterSelectEntered(const Save::SaveProgress& progress, int playerCount,
                                  std::array<int, kMpMaxPlayers>& playerChar,
                                  const std::array<bool, kMpMaxPlayers>& hasSelected);

  private:
    int numChars_ = kMpCharsDefault;
};

} // namespace Frigate::Game
