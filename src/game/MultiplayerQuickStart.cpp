#include "game/MultiplayerQuickStart.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "game/menu/CheatIds.h"
#include "level/PlayerMove.h"

namespace Frigate::Game {

namespace {

constexpr std::array<const char*, 12> kStageNames = {
    "random", "temple",   "complex", "caves",    "library", "basement",
    "stack",  "facility", "bunker",  "archives", "caverns", "egypt",
};
constexpr std::array<const char*, 8> kScenarioNames = {
    "normal", "yolt", "daylights", "goldengun", "licence", "2v2", "3v1", "2v1",
};
constexpr std::array<const char*, 8> kGameLengthNames = {
    "unlimited", "5min", "10min", "20min", "5points", "10points", "20points", "lastalive",
};
constexpr std::array<const char*, MultiplayerSettings::kWeaponSets> kWeaponSetNames = {
    "slappers",    "pistols",   "knives",     "automatics", "power",   "snipers", "grenades",
    "remotemines", "launchers", "timedmines", "proxmines",  "rockets", "lasers",  "goldengun",
};

// The cheats a game can switch on, indexed by id - 1.
constexpr std::array<const char*, Menu::Cheat::k2xLaser> kCheatNames = {
    "mpchars",        "invincible",
    "allguns",        "maxammo",
    "debug5",         "deactivateinvincible",
    "linemode",       "2xhealth",
    "2xarmor",        "invisible",
    "infiniteammo",   "dkmode",
    "extraweapons",   "tinybond",
    "paintball",      "10xhealth",
    "magnum",         "laser",
    "goldengun",      "silverpp7",
    "goldpp7",        "bondphase",
    "noradar",        "turbo",
    "debugpos",       "fast",
    "slow",           "enemyrockets",
    "2xrocket",       "2xgrenade",
    "2xrcp90",        "2xthrowingknife",
    "2xhuntingknife", "2xlaser",
};

void Fail(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string Normalized(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::optional<int> ParseInt(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

// The row of `names` the text names, by name or number.
template <size_t N> std::optional<int> FindRow(const std::array<const char*, N>& names, const std::string& text) {
    const std::string wanted = Normalized(text);
    for (size_t row = 0; row < N; row++) {
        if (wanted == Normalized(names[row])) {
            return static_cast<int>(row);
        }
    }
    if (const auto number = ParseInt(text); number && *number >= 0 && *number < static_cast<int>(N)) {
        return number;
    }
    return std::nullopt;
}

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) {
            return parts;
        }
        start = comma + 1;
    }
}

// Fills `out` for `players` players from a one-value or per-player list.
template <typename T, typename Parse>
bool FillPerPlayer(const char* what, const std::string& text, int players, Parse parse, std::array<T, 4>& out,
                   std::string* error) {
    if (text.empty()) {
        return true;
    }
    const std::vector<std::string> parts = Split(text);
    if (parts.size() != 1 && parts.size() != static_cast<size_t>(players)) {
        Fail(error, std::string(what) + ": give one value or one per player (" + std::to_string(players) + ")");
        return false;
    }
    for (int player = 0; player < players; player++) {
        const std::string& part = parts[parts.size() == 1 ? 0 : static_cast<size_t>(player)];
        const auto value = parse(part);
        if (!value) {
            Fail(error, std::string(what) + ": '" + part + "' is not a value");
            return false;
        }
        out[static_cast<size_t>(player)] = *value;
    }
    return true;
}

template <size_t N>
bool FillRow(const char* what, const std::array<const char*, N>& names, const std::string& text, int& out,
             std::string* error) {
    if (text.empty()) {
        return true;
    }
    const auto row = FindRow(names, text);
    if (!row) {
        Fail(error, std::string(what) + ": '" + text + "' is not one of the table's names or rows");
        return false;
    }
    out = *row;
    return true;
}

} // namespace

const char* StageName(MpStage stage) {
    return kStageNames[static_cast<size_t>(stage)];
}

const char* ScenarioName(MpScenario scenario) {
    return kScenarioNames[static_cast<size_t>(scenario)];
}

const char* GameLengthName(MpGameLength length) {
    return kGameLengthNames[static_cast<size_t>(length)];
}

const char* WeaponSetName(int weaponSet) {
    return kWeaponSetNames[static_cast<size_t>(weaponSet)];
}

const char* CheatName(int32_t cheat) {
    if (cheat < 1 || cheat > static_cast<int32_t>(kCheatNames.size())) {
        return nullptr;
    }
    return kCheatNames[static_cast<size_t>(cheat - 1)];
}

std::optional<int> TeamSizeFor(MpScenario scenario, int players, std::string_view teams, std::string* error) {
    if (teams.size() != static_cast<size_t>(players)) {
        Fail(error, "teams: one letter per player (" + std::to_string(players) + ")");
        return std::nullopt;
    }
    std::array<std::vector<int>, 2> members;
    for (int player = 0; player < players; player++) {
        const char team =
            static_cast<char>(std::toupper(static_cast<unsigned char>(teams[static_cast<size_t>(player)])));
        if (team != 'A' && team != 'B') {
            Fail(error, "teams: each letter is A or B");
            return std::nullopt;
        }
        members[team == 'A' ? 0 : 1].push_back(player);
    }
    const std::vector<int>& withFirst = members[teams[0] == 'A' || teams[0] == 'a' ? 0 : 1];
    if (scenario == MpScenario::TwoVsTwo && members[0].size() == 2 && members[1].size() == 2) {
        return withFirst[1];
    }
    if (scenario == MpScenario::ThreeVsOne || scenario == MpScenario::TwoVsOne) {
        for (const auto& team : members) {
            if (team.size() == 1 && team.size() < static_cast<size_t>(players) - 1) {
                return team[0];
            }
        }
    }
    Fail(error, "teams: the split does not fit the scenario");
    return std::nullopt;
}

std::optional<MultiplayerSettings> ParseQuickStart(const QuickStartArgs& args, std::string* error) {
    if (args.players < 1 || args.players > kMpMaxPlayers) {
        Fail(error, "players: 1 to 4, not " + std::to_string(args.players));
        return std::nullopt;
    }
    MultiplayerSettings settings = MultiplayerSettings::Defaults(args.players);
    int row = static_cast<int>(settings.stage);
    if (!FillRow("stage", kStageNames, args.stage, row, error)) {
        return std::nullopt;
    }
    settings.stage = static_cast<MpStage>(row);
    row = static_cast<int>(settings.scenario);
    if (!FillRow("scenario", kScenarioNames, args.scenario, row, error)) {
        return std::nullopt;
    }
    settings.scenario = static_cast<MpScenario>(row);
    row = static_cast<int>(settings.gameLength);
    if (!FillRow("game length", kGameLengthNames, args.gameLength, row, error)) {
        return std::nullopt;
    }
    settings.gameLength = static_cast<MpGameLength>(row);
    if (!FillRow("weapon set", kWeaponSetNames, args.weaponSet, settings.weaponSet, error)) {
        return std::nullopt;
    }
    if (!args.aim.empty()) {
        const auto aim = ParseInt(args.aim);
        if (!aim || *aim < 0 || *aim >= MultiplayerSettings::kAimRows) {
            Fail(error, "aim: 0 to 3");
            return std::nullopt;
        }
        settings.aimSetting = *aim;
    }
    const auto number = [](const std::string& text) { return ParseInt(text); };
    const auto style = [](const std::string& text) { return Level::ParseControlStyle(text); };
    if (!FillPerPlayer("characters", args.characters, args.players, number, settings.character, error) ||
        !FillPerPlayer("handicaps", args.handicaps, args.players, number, settings.handicap, error) ||
        !FillPerPlayer("control styles", args.controlStyles, args.players, style, settings.controlStyle, error)) {
        return std::nullopt;
    }
    if (!args.teams.empty()) {
        if (!settings.IsTeamScenario()) {
            Fail(error, "teams: the scenario has no teams");
            return std::nullopt;
        }
        const auto teamSize = TeamSizeFor(settings.scenario, args.players, args.teams, error);
        if (!teamSize) {
            return std::nullopt;
        }
        settings.teamSize = *teamSize;
    }
    if (!args.cheats.empty()) {
        for (const std::string& part : Split(args.cheats)) {
            const std::string wanted = Normalized(part);
            int32_t found = 0;
            for (size_t id = 1; id <= kCheatNames.size(); id++) {
                if (wanted == Normalized(kCheatNames[id - 1])) {
                    found = static_cast<int32_t>(id);
                }
            }
            const auto number = ParseInt(part);
            if (found == 0 && number && *number >= 1 && *number <= static_cast<int>(kCheatNames.size())) {
                found = *number;
            }
            if (found == 0) {
                Fail(error, "cheats: '" + part + "' is not a cheat name or id");
                return std::nullopt;
            }
            if (std::find(settings.cheats.begin(), settings.cheats.end(), found) == settings.cheats.end()) {
                settings.cheats.push_back(found);
            }
        }
    }
    const char* reason = nullptr;
    if (!settings.Valid(&reason)) {
        Fail(error, reason);
        return std::nullopt;
    }
    return settings;
}

void ResolveRandomStage(MultiplayerSettings& settings, uint32_t random) {
    if (settings.stage != MpStage::Random) {
        return;
    }
    std::array<MpStage, static_cast<size_t>(MpStage::Count)> fits{};
    size_t count = 0;
    for (int stage = static_cast<int>(MpStage::Random) + 1; stage < static_cast<int>(MpStage::Count); stage++) {
        if (MultiplayerSettings::StagePlayers(static_cast<MpStage>(stage)).max >= settings.playerCount) {
            fits[count++] = static_cast<MpStage>(stage);
        }
    }
    settings.stage = fits[random % count];
}

uint8_t PortsForPlayers(int players) {
    return static_cast<uint8_t>((1 << std::clamp(players, 0, kMpMaxPlayers)) - 1);
}

} // namespace Frigate::Game
