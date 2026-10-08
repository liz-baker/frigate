#pragma once

#include <array>

#include "game/MultiplayerSettings.h"

namespace Frigate::Game::MpOptions {

// The multiplayer options screen's rows that a scenario locks (the unlock_*
// globals reset_mp_options_for_scenario sets). A locked row is drawn dimmed and
// the cursor skips it. Nothing but that function ever changes the scenario, so
// they follow from it.
struct RowUnlocks {
    bool stageSelect = true;
    bool gameLength = true;
    bool chars = true;
    bool weaponSelect = true;
    bool handicap = true;
    bool controlStyle = true;
    bool aimSight = true;
};
RowUnlocks UnlocksFor(MpScenario scenario);

// reset_mp_options_for_scenario: makes `scenario` the scenario and fits the game
// length and weapon set to what it allows.
void ResetForScenario(MultiplayerSettings& settings, MpScenario scenario);

// init_mp_options_for_scenario: sets the player count (never fewer than two)
// and gives each of those players their own character and the normal handicap
// if they have none yet. With three or more players the two-controller control
// styles are reset. The scenario falls back to Normal and the stage to Temple
// when they have no room for that many players.
void InitForPlayerCount(MultiplayerSettings& settings, int numPlayers);

// select_game_length: the next game length; Flag Tag cycles through the four
// timed ones, every other scenario through the seven without Last Alive.
void SelectGameLength(MultiplayerSettings& settings);

// incrementMPWeaponSet.
void IncrementWeaponSet(MultiplayerSettings& settings);

// advance_aim_settings_selection's change to the setting.
void AdvanceAimSetting(MultiplayerSettings& settings);

// copy_aim_settings_to_playerdata: the autoaim and sight each player's data
// gets.
std::array<MpAimSetting, kMpMaxPlayers> CopyAimSettingsToPlayerData(const MultiplayerSettings& settings);

// mp_weapon_set_text_table's text for a weapon set: the slot in the
// multiplayer weapons text bank (LMPWEAPONS).
int WeaponSetTextSlot(int weaponSet);

} // namespace Frigate::Game::MpOptions
