#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "game/MultiplayerSettings.h"

// Builds the MultiplayerSettings of a game started without the menus, from the
// text the launch arguments carry (docs/multiplayer-quick-start.md).
namespace Frigate::Game {

// One field per argument; an empty one leaves that part of the settings at the
// defaults for the player count. A per-player list ("a,b,c,d") takes either
// one value for every player or one per player.
struct QuickStartArgs {
    int players = 2;
    std::string stage;
    std::string scenario;
    std::string characters;
    std::string handicaps;
    std::string controlStyles;
    // One letter per player naming their team in a team scenario ("AABB").
    std::string teams;
    std::string weaponSet;
    std::string gameLength;
    std::string aim;
    // Comma-separated cheat names or ids (CheatName): the cheats switched on, as
    // the cheat menu would before the game is picked.
    std::string cheats;
};

// The lowercase name the arguments take for each table row (a row's number is
// accepted too).
const char* StageName(MpStage stage);
const char* ScenarioName(MpScenario scenario);
const char* GameLengthName(MpGameLength length);
const char* WeaponSetName(int weaponSet);
// The lowercase name the arguments take for a cheat id from CHEAT_EXTRA_MP_CHARS
// to CHEAT_2X_LASER (its number is accepted too); null for any other id.
const char* CheatName(int32_t cheat);

// The settings the arguments describe, or nothing with `error` saying what is
// wrong (a name that is not one, a list of the wrong length, or settings that
// MultiplayerSettings::Valid refuses).
std::optional<MultiplayerSettings> ParseQuickStart(const QuickStartArgs& args, std::string* error = nullptr);

// MultiplayerSettings::teamSize for the teams the letters (A or B) name: in a
// 2 vs 2 the player who shares player 1's team, in a 3 vs 1 or 2 vs 1 the
// lone player. Nothing, with `error`, when the split is not the scenario's.
std::optional<int> TeamSizeFor(MpScenario scenario, int players, std::string_view teams, std::string* error = nullptr);

// Replaces a Random stage with one the player count fits, picked by
// `random`. Other stages are left alone.
void ResolveRandomStage(MultiplayerSettings& settings, uint32_t random);

// The controller ports (bit n = port n) a game for `players` players needs to
// read as connected.
uint8_t PortsForPlayers(int players);

} // namespace Frigate::Game
