#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "level/LevelInfo.h"
#include "level/PlayerMove.h"

namespace Frigate::Game {

constexpr int kMpMaxPlayers = 4;

// MPSCENARIOS.
enum class MpScenario : int32_t {
    Normal,
    YouOnlyLiveTwice,
    TheLivingDaylights,
    TheManWithTheGoldenGun,
    LicenceToKill,
    TwoVsTwo,
    ThreeVsOne,
    TwoVsOne,
    Count,
};

// MP_STAGE_SELECTED: the multiplayer stage menu's rows. Random is resolved to
// one of the others when the game starts.
enum class MpStage : int32_t {
    Random,
    Temple,
    Complex,
    Caves,
    Library,
    Basement,
    Stack,
    Facility,
    Bunker,
    Archives,
    Caverns,
    Egypt,
    Count,
};

// multi_game_lengths' rows.
enum class MpGameLength : int32_t {
    Unlimited,
    FiveMinutes,
    TenMinutes,
    TwentyMinutes,
    FivePoints,
    TenPoints,
    TwentyPoints,
    LastAlive,
    Count,
};

// mp_sight_adjust_table's rows.
struct MpAimSetting {
    int autoaim = 0;
    int sight = 0;
};

// How many players a scenario or stage allows (mp_player_counts and
// multi_stage_setups).
struct MpPlayerRange {
    int min = 0;
    int max = 0;
};

// The multiplayer front-end's globals (front.c, mp_weapon.c) that a stage load
// reads: what the menus choose and lvlStageLoad applies.
struct MultiplayerSettings {
    static constexpr int kNormalHandicap = 5;
    static constexpr int kDefaultWeaponSet = 0xB;
    static constexpr int kHandicapRows = 11;
    static constexpr int kCharacterRows = 64;
    static constexpr int kAimRows = 4;
    static constexpr int kWeaponSets = 14;

    // selected_num_players, 1 to 4. The front-end only offers 2 to 4; a
    // one-player value runs the stage as a solo game.
    int playerCount = 2;
    // player_char: a row of mp_chr_setup per player, negative for the
    // player's own row.
    std::array<int, kMpMaxPlayers> character{ 0, 1, 2, 3 };
    // player_handicap: a row of MP_handicap_table per player.
    std::array<int, kMpMaxPlayers> handicap{ kNormalHandicap, kNormalHandicap, kNormalHandicap, kNormalHandicap };
    // controlstyle_player.
    std::array<Level::ControlConfig, kMpMaxPlayers> controlStyle{ Level::ControlConfig::Honey,
                                                                  Level::ControlConfig::Honey,
                                                                  Level::ControlConfig::Honey,
                                                                  Level::ControlConfig::Honey };
    // scenario.
    MpScenario scenario = MpScenario::Normal;
    // teamsize: which player the team select put in the second team (see
    // TeamFlag).
    int teamSize = 3;
    // MP_stage_selected.
    MpStage stage = MpStage::Temple;
    // game_length.
    MpGameLength gameLength = MpGameLength::TenMinutes;
    // mp_weapon_set: a row of mp_weapon_set_text_table.
    int weaponSet = kDefaultWeaponSet;
    // aim_sight_adjustment: a row of mp_sight_adjust_table.
    int aimSetting = 3;

    // The settings init_mp_options_for_scenario leaves for `players` players:
    // each player with their own character and the normal handicap.
    static MultiplayerSettings Defaults(int players);

    // MP_handicap_table's damage_modifier for the player (get_player_mp_handicap).
    float Handicap(int player) const;
    // mp_chr_setup's pov for the player's character (get_player_mp_char_height).
    float CharacterHeight(int player) const;
    // multi_game_lengths' time (in clock ticks) and points; 0 for none
    // (get_mp_timelimit and get_mp_pointlimit).
    int32_t TimeLimit() const;
    int32_t PointLimit() const;
    // mp_sight_adjust_table[aim_sight_adjustment].
    MpAimSetting Aim() const;
    // The flag the team select stores for the player with
    // set_players_team_or_scenario_item_flag: 0 for the first team and 1 for
    // the second. 0 for any scenario without teams.
    int TeamFlag(int player) const;
    bool IsTeamScenario() const;
    // The stage's level, or nothing while the stage is still Random.
    std::optional<Level::LevelId> StageLevel() const;

    // mp_player_counts and multi_stage_setups.
    static MpPlayerRange ScenarioPlayers(MpScenario scenario);
    static MpPlayerRange StagePlayers(MpStage stage);

    // Whether the settings can start a game: the player count within the
    // scenario's and stage's range and every table row in range. `reason`
    // says what is wrong.
    bool Valid(const char** reason = nullptr) const;
};

} // namespace Frigate::Game
