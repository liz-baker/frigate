#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "game/MultiplayerSettings.h"

// The scoring and awards of a multiplayer game (mpmenu.c): the points a player
// has for the scenario, the rank those make, the extremes the awards look for
// and the awards each player is shown. Plain data in and out, so the rules can
// be checked without a stage.
namespace Frigate::Level {

constexpr int kMpPlayers = Game::kMpMaxPlayers;

// randomGetNext, which the ties and the choice of award draw from.
using MpRandom = std::function<uint32_t()>;

// AWARD: the bit of each award, and so the index of its name in g_AwardNames.
namespace Award {
constexpr uint32_t MostSuicidal = 0x00001;
constexpr uint32_t WhoNeedsAmmo = 0x00002;
constexpr uint32_t WheresTheArmour = 0x00004;
constexpr uint32_t AcNegative10 = 0x00008;
constexpr uint32_t Marksmanship = 0x00010;
constexpr uint32_t MostProfessional = 0x00020;
constexpr uint32_t MostDeadly = 0x00040;
constexpr uint32_t MostHarmless = 0x00080;
constexpr uint32_t MostCowardly = 0x00100;
constexpr uint32_t MostFrantic = 0x00200;
constexpr uint32_t MostHonorable = 0x00400;
constexpr uint32_t MostDishonorable = 0x00800;
constexpr uint32_t ShortestInnings = 0x01000;
constexpr uint32_t LongestInnings = 0x02000;
constexpr uint32_t DoubleKill = 0x04000;
constexpr uint32_t TripleKill = 0x08000;
constexpr uint32_t QuadKill = 0x10000;
} // namespace Award

// The number of awards (g_AwardNames' length).
constexpr int kAwardCount = 17;

// mpFindMaxInt, mpFindMinInt, mpFindMaxFloat and mpFindMinFloat: the index of
// the player (of the first `players`) with the highest or lowest value. A tie
// goes to the later player when `random` is odd, so every tie draws a number.
// The float forms compare against the best so far truncated to an integer, as
// the original does.
int FindMaxInt(int players, const std::array<int32_t, kMpPlayers>& values, const MpRandom& random);
int FindMinInt(int players, const std::array<int32_t, kMpPlayers>& values, const MpRandom& random);
int FindMaxFloat(int players, const std::array<float, kMpPlayers>& values, const MpRandom& random);
int FindMinFloat(int players, const std::array<float, kMpPlayers>& values, const MpRandom& random);

// What a game leaves of each player, as get_points_for_mp_player and
// write_playerrank_to_buffer read it from g_playerPlayerData.
struct ScoreBoard {
    Game::MpScenario scenario = Game::MpScenario::Normal;
    int players = 2;
    // kill_counts[killer][victim]; a player's own entry counts their suicides.
    std::array<std::array<int32_t, kMpPlayers>, kMpPlayers> killCounts{};
    // have_token_or_goldengun, which the team scenarios use as the team.
    std::array<int32_t, kMpPlayers> teamOrToken{};
    // killed_gg_owner_count.
    std::array<int32_t, kMpPlayers> killedGoldenGunOwner{};
    // order_out_in_yolt.
    std::array<int32_t, kMpPlayers> orderOutInYolt{};
    // flag_counter.
    std::array<int32_t, kMpPlayers> flagCounter{};
};

// get_points_for_mp_player.
int32_t PointsFor(const ScoreBoard& board, int player);

// write_playerrank_to_buffer's rank: 0 for first, up to 3. The players are
// sorted by points (a stable pass, so a tie keeps player order), and a player
// takes the place of the first player with the same points. In a team
// scenario the places that cannot exist (third in a 2 vs 2 or 2 vs 1, fourth in
// a 3 vs 1) read as second.
int RankOf(const ScoreBoard& board, int player);

// mpwatchShouldDisplayRank and mpwatchShouldDisplayScore for the scenario;
// `param` is the page's second argument (the game over screen).
bool ShouldDisplayRank(Game::MpScenario scenario, bool param);
bool ShouldDisplayScore(Game::MpScenario scenario, bool param);

// AwardMetrics, per player.
struct AwardMetrics {
    int32_t shots = 0;
    int32_t headshots = 0;
    int32_t kills = 0;
    int32_t deaths = 0;
    int32_t suicides = 0;
    float ksRatio = 0.0f;
    float kdRatio = 0.0f;
    int32_t damageToBackside = 0;
    int32_t timeOtherPlayersOnScreen = 0;
    float avgKmPerHour = 0.0f;
    float bodyArmourPickups = 0.0f;
    uint32_t awards = 0;
    int32_t longestInning = 0;
    int32_t shortestInning = 0;
};

// What mpCalculateAwards reads of one player's stats.
struct AwardInput {
    int32_t shotsTotal = 0;
    int32_t shotsHead = 0;
    int32_t damageToBackside = 0;
    int32_t timeOtherPlayersOnScreen = 0;
    float distanceTraveled = 0.0f;
    float bodyArmourPickups = 0.0f;
    int32_t longestInning = 0;
    int32_t shortestInning = 0;
    // most_killed_one_time: the most kills in one streak.
    int32_t mostKilledOneTime = 0;
};

// The metrics of each player: kills, deaths and suicides from `killCounts`,
// the ratios and the average speed over a game of `duration` ticks.
std::array<AwardMetrics, kMpPlayers> BuildMetrics(int players, const std::array<AwardInput, kMpPlayers>& inputs,
                                                  const std::array<std::array<int32_t, kMpPlayers>, kMpPlayers>& kills,
                                                  int32_t duration);

// The awards each player is eligible for, set in `metrics[].awards`.
void ChooseEligibleAwards(int players, std::array<AwardMetrics, kMpPlayers>& metrics,
                          const std::array<AwardInput, kMpPlayers>& inputs, const MpRandom& random);

// The two awards a player is shown (an index into g_AwardNames, -1 for none).
struct AwardPick {
    int first = -1;
    int second = -1;
};

// The pick of the two awards to show each player from those they are eligible
// for. A quad kill, if there is one, is always first; any other award is picked
// at random.
std::array<AwardPick, kMpPlayers> PickAwards(int players, std::array<AwardMetrics, kMpPlayers>& metrics,
                                             const MpRandom& random);

} // namespace Frigate::Level
