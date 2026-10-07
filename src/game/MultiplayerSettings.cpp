#include "game/MultiplayerSettings.h"

namespace Frigate::Game {

namespace {

// MP_handicap_table's damage_modifier.
constexpr std::array<float, MultiplayerSettings::kHandicapRows> kHandicap = {
    10.0f, 2.8560996f, 2.1969998f, 1.6899998f, 1.3f, 1.0f, 0.76923078f, 0.59171599f, 0.45516616f, 0.35012782f, 0.1f,
};

// mp_chr_setup's pov.
constexpr std::array<float, MultiplayerSettings::kCharacterRows> kCharacterHeight = {
    1.0f,        0.96609998f, 1.0f,    1.0f,    1.0778f, 0.9702f, 0.93239999f, 1.0f,    1.0f,    1.199f,  0.78780001f,
    1.0f,        1.0f,        1.0f,    1.0f,    1.0f,    1.0f,    1.0f,        1.0f,    1.0f,    1.0f,    1.0f,
    1.0f,        1.0f,        1.0f,    1.0f,    1.0f,    1.0f,    1.0f,        1.0f,    1.0f,    1.0f,    1.0f,
    0.88529998f, 1.0446f,     1.0446f, 1.0446f, 1.0446f, 1.0446f, 1.0446f,     1.0446f, 1.0446f, 1.0446f, 1.0446f,
    1.0446f,     1.0446f,     1.0446f, 1.0446f, 1.0446f, 1.0446f, 1.0446f,     1.0446f, 1.0446f, 1.0446f, 1.0446f,
    1.0446f,     1.0446f,     1.0446f, 1.0446f, 1.0446f, 1.0446f, 1.0446f,     1.0446f, 1.0446f,
};

// multi_game_lengths' time (MINS_TO_TIMER60 of its minutes) and points.
struct GameLength {
    int32_t time;
    int32_t points;
};
constexpr int32_t kTicksPerMinute = 60 * 60;
constexpr std::array<GameLength, 8> kGameLengths = { {
    { 0, 0 },
    { 5 * kTicksPerMinute, 0 },
    { 10 * kTicksPerMinute, 0 },
    { 20 * kTicksPerMinute, 0 },
    { 0, 5 },
    { 0, 10 },
    { 0, 20 },
    { 0, 0 },
} };

// mp_sight_adjust_table's autoaim and sight.
constexpr std::array<MpAimSetting, MultiplayerSettings::kAimRows> kAim = { {
    { 0, 0 },
    { 0, 1 },
    { 1, 0 },
    { 1, 1 },
} };

// mp_player_counts.
constexpr std::array<MpPlayerRange, 8> kScenarioPlayers = { {
    { 2, 4 },
    { 2, 4 },
    { 2, 4 },
    { 2, 4 },
    { 2, 4 },
    { 4, 4 },
    { 4, 4 },
    { 3, 3 },
} };

// multi_stage_setups' max_player.
constexpr std::array<MpPlayerRange, 12> kStagePlayers = { {
    { 1, 4 },
    { 1, 4 },
    { 1, 4 },
    { 1, 4 },
    { 1, 4 },
    { 1, 4 },
    { 1, 4 },
    { 1, 4 },
    { 1, 3 },
    { 1, 3 },
    { 1, 3 },
    { 1, 2 },
} };

bool InRange(int value, int count) {
    return value >= 0 && value < count;
}

} // namespace

MultiplayerSettings MultiplayerSettings::Defaults(int players) {
    MultiplayerSettings settings;
    settings.playerCount = players;
    return settings;
}

float MultiplayerSettings::Handicap(int player) const {
    return kHandicap[static_cast<size_t>(handicap[static_cast<size_t>(player)])];
}

float MultiplayerSettings::CharacterHeight(int player) const {
    const int row = character[static_cast<size_t>(player)];
    return kCharacterHeight[static_cast<size_t>(row < 0 ? player : row)];
}

int32_t MultiplayerSettings::TimeLimit() const {
    return kGameLengths[static_cast<size_t>(gameLength)].time;
}

int32_t MultiplayerSettings::PointLimit() const {
    return kGameLengths[static_cast<size_t>(gameLength)].points;
}

MpAimSetting MultiplayerSettings::Aim() const {
    return kAim[static_cast<size_t>(aimSetting)];
}

bool MultiplayerSettings::IsTeamScenario() const {
    return scenario == MpScenario::TwoVsTwo || scenario == MpScenario::ThreeVsOne || scenario == MpScenario::TwoVsOne;
}

int MultiplayerSettings::TeamFlag(int player) const {
    if (scenario == MpScenario::TwoVsTwo) {
        return player == 0 || player == teamSize ? 0 : 1;
    }
    if (scenario == MpScenario::ThreeVsOne || scenario == MpScenario::TwoVsOne) {
        return player == teamSize ? 1 : 0;
    }
    return 0;
}

std::optional<Level::LevelId> MultiplayerSettings::StageLevel() const {
    using Level::LevelId;
    switch (stage) {
        case MpStage::Temple:
            return LevelId::Temple;
        case MpStage::Complex:
            return LevelId::Complex;
        case MpStage::Caves:
            return LevelId::Caves;
        case MpStage::Library:
            return LevelId::Library;
        case MpStage::Basement:
            return LevelId::Basement;
        case MpStage::Stack:
            return LevelId::Stack;
        case MpStage::Facility:
            return LevelId::Facility;
        case MpStage::Bunker:
            return LevelId::Bunker2;
        case MpStage::Archives:
            return LevelId::Archives;
        case MpStage::Caverns:
            return LevelId::Caverns;
        case MpStage::Egypt:
            return LevelId::Egypt;
        default:
            return std::nullopt;
    }
}

MpPlayerRange MultiplayerSettings::ScenarioPlayers(MpScenario scenario) {
    return kScenarioPlayers[static_cast<size_t>(scenario)];
}

MpPlayerRange MultiplayerSettings::StagePlayers(MpStage stage) {
    return kStagePlayers[static_cast<size_t>(stage)];
}

bool MultiplayerSettings::Valid(const char** reason) const {
    const char* why = nullptr;
    if (!InRange(playerCount - 1, kMpMaxPlayers)) {
        why = "the player count is not 1 to 4";
    } else if (!InRange(static_cast<int>(scenario), static_cast<int>(MpScenario::Count)) ||
               !InRange(static_cast<int>(stage), static_cast<int>(MpStage::Count)) ||
               !InRange(static_cast<int>(gameLength), static_cast<int>(MpGameLength::Count)) ||
               !InRange(weaponSet, kWeaponSets) || !InRange(aimSetting, kAimRows)) {
        why = "a scenario, stage, game length, weapon set or aim setting is out of range";
    } else if (playerCount > 1 &&
               (playerCount < ScenarioPlayers(scenario).min || playerCount > ScenarioPlayers(scenario).max)) {
        why = "the scenario does not allow this many players";
    } else if (playerCount > StagePlayers(stage).max) {
        why = "the stage does not allow this many players";
    } else {
        for (int player = 0; player < playerCount; player++) {
            const size_t index = static_cast<size_t>(player);
            if (character[index] >= kCharacterRows || !InRange(handicap[index], kHandicapRows) ||
                !InRange(static_cast<int>(controlStyle[index]), static_cast<int>(Level::ControlConfig::Cinema))) {
                why = "a player's character, handicap or control style is out of range";
                break;
            }
        }
    }
    if (reason != nullptr) {
        *reason = why;
    }
    return why == nullptr;
}

} // namespace Frigate::Game
