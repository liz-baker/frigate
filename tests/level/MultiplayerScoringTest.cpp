#include "level/MultiplayerScoring.h"

#include <cstdio>
#include <vector>

using namespace Frigate::Level;
using Frigate::Game::MpScenario;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// A random source that plays back fixed numbers and counts the draws; once the
// list runs out it keeps returning the last number.
class Script {
  public:
    explicit Script(std::vector<uint32_t> values) : values_(std::move(values)) {
    }
    MpRandom Source() {
        return [this]() -> uint32_t {
            draws_++;
            if (values_.empty()) {
                return 0;
            }
            const uint32_t value = values_[next_ < values_.size() ? next_ : values_.size() - 1];
            next_++;
            return value;
        };
    }
    int Draws() const {
        return draws_;
    }

  private:
    std::vector<uint32_t> values_;
    size_t next_ = 0;
    int draws_ = 0;
};

using Ints = std::array<int32_t, kMpPlayers>;
using Floats = std::array<float, kMpPlayers>;

void TestFindInt() {
    for (int players = 2; players <= 4; players++) {
        Script none({});
        const Ints rising{ 1, 2, 3, 4 };
        Check(FindMaxInt(players, rising, none.Source()) == players - 1, "the highest value of a rising run");
        Check(FindMinInt(players, rising, none.Source()) == 0, "the lowest value of a rising run");
        Check(none.Draws() == 0, "no tie draws no number");
    }

    // Every tie draws a number, and an odd one gives the lead to the later
    // player.
    for (int players = 2; players <= 4; players++) {
        const Ints equal{ 5, 5, 5, 5 };
        Script odd({ 1 });
        Check(FindMaxInt(players, equal, odd.Source()) == players - 1, "odd draws pass the lead to the last player");
        Check(odd.Draws() == players - 1, "one draw for each comparison");
        Script even({ 0 });
        Check(FindMaxInt(players, equal, even.Source()) == 0, "even draws keep the first player ahead");
        Check(FindMinInt(players, equal, even.Source()) == 0, "even draws keep the first player lowest");
        Script oddMin({ 1 });
        Check(FindMinInt(players, equal, oddMin.Source()) == players - 1, "odd draws pass the low to the last player");
    }

    // A tie only for the second place of three draws once.
    {
        Script script({ 1 });
        const Ints values{ 7, 3, 3, 0 };
        Check(FindMaxInt(3, values, script.Source()) == 0, "three players: the first is highest");
        Check(script.Draws() == 0, "the later tie is for a value that is not the best");
    }

    // The fourth player is ignored in a game of three.
    {
        Script none({});
        const Ints values{ 1, 2, 3, 100 };
        Check(FindMaxInt(3, values, none.Source()) == 2, "a player past the count is not considered");
    }
}

void TestFindFloat() {
    Script none({});
    const Floats values{ 1.0f, 2.5f, 0.5f, 3.5f };
    Check(FindMaxFloat(2, values, none.Source()) == 1, "two players: the higher float");
    Check(FindMaxFloat(4, values, none.Source()) == 3, "four players: the highest float");
    Check(FindMinFloat(4, values, none.Source()) == 2, "four players: the lowest float");
    Check(none.Draws() == 0, "no ties in the floats");

    // The best so far is truncated to an integer, so a later value that is
    // above that truncated value but below the best itself still wins.
    const Floats truncated{ 0.0f, 2.7f, 2.2f, 0.0f };
    Check(FindMaxFloat(3, truncated, none.Source()) == 2, "the best so far is truncated for the max");
    const Floats truncatedLow{ 9.0f, 2.7f, 2.2f, 0.0f };
    Check(FindMinFloat(3, truncatedLow, none.Source()) == 1, "the best so far is truncated for the min");
    const Floats exact{ 0.0f, 2.0f, 2.0f, 0.0f };
    Script odd({ 1 });
    Check(FindMaxFloat(3, exact, odd.Source()) == 2, "a tie with the truncated best draws");
}

ScoreBoard BoardFor(MpScenario scenario, int players) {
    ScoreBoard board;
    board.scenario = scenario;
    board.players = players;
    return board;
}

void TestNormalPoints() {
    for (int players = 2; players <= 4; players++) {
        ScoreBoard board = BoardFor(MpScenario::Normal, players);
        // Player 0 kills each other player twice and kills themselves once.
        for (int victim = 1; victim < players; victim++) {
            board.killCounts[0][static_cast<size_t>(victim)] = 2;
        }
        board.killCounts[0][0] = 1;
        Check(PointsFor(board, 0) == 2 * (players - 1) - 1, "kills less suicides");
        // Player 1 was killed twice and never killed anyone: the deaths do not
        // count against a score.
        Check(PointsFor(board, 1) == 0, "deaths are not points");
        if (players >= 3) {
            // The last player kills player 1 once.
            board.killCounts[static_cast<size_t>(players - 1)][1] = 1;
            Check(PointsFor(board, players - 1) == 1, "a single kill");
            board.killCounts[static_cast<size_t>(players - 1)][1] = 0;
        }

        board.killedGoldenGunOwner[0] = 3;
        Check(PointsFor(board, 0) == 2 * (players - 1) - 1 + 3 * (players - 2),
              "golden gun kills count for the players beyond two");
    }

    // Licence to Kill and The Man With The Golden Gun score the same way.
    ScoreBoard board = BoardFor(MpScenario::LicenceToKill, 3);
    board.killCounts[1][0] = 4;
    board.killCounts[1][1] = 1;
    Check(PointsFor(board, 1) == 3, "Licence to Kill: kills less suicides");
    board.scenario = MpScenario::TheManWithTheGoldenGun;
    board.killedGoldenGunOwner[1] = 2;
    Check(PointsFor(board, 1) == 5, "The Man With The Golden Gun: golden gun kills add to the score");
}

void TestYoltAndFlagPoints() {
    for (int players = 2; players <= 4; players++) {
        ScoreBoard board = BoardFor(MpScenario::YouOnlyLiveTwice, players);
        // The last player out has order 0 (never out); the first out is
        // players (everyone still standing + 1).
        for (int player = 0; player < players; player++) {
            board.orderOutInYolt[static_cast<size_t>(player)] = player;
        }
        for (int player = 0; player < players; player++) {
            Check(PointsFor(board, player) == kMpPlayers - player,
                  "You Only Live Twice scores four less the order out");
        }
    }

    ScoreBoard board = BoardFor(MpScenario::TheLivingDaylights, 4);
    board.flagCounter = { 120, 0, 600, 5 };
    board.killCounts[0][1] = 9;
    Check(PointsFor(board, 0) == 120 && PointsFor(board, 2) == 600 && PointsFor(board, 1) == 0,
          "Flag Tag scores the flag counter and ignores kills");
}

void TestTeamPoints() {
    // 2 vs 2: players 0 and 1 against 2 and 3.
    {
        ScoreBoard board = BoardFor(MpScenario::TwoVsTwo, 4);
        board.teamOrToken = { 0, 0, 1, 1 };
        board.killCounts[0][2] = 3;
        board.killCounts[1][3] = 1;
        board.killCounts[2][0] = 1;
        board.killCounts[1][0] = 2; // a team kill
        board.killCounts[3][3] = 1; // a suicide
        // Team 0: kills of the other team 3 + 1 = 4, less 2 for the team kill.
        // Team 1: 1 kill, less the suicide.
        Check(PointsFor(board, 0) == 2 && PointsFor(board, 1) == 2, "a 2 vs 2 team shares its members' score");
        Check(PointsFor(board, 2) == 0 && PointsFor(board, 3) == 0, "the other team shares theirs");
    }
    // 3 vs 1: players 0, 1 and 2 against 3.
    {
        ScoreBoard board = BoardFor(MpScenario::ThreeVsOne, 4);
        board.teamOrToken = { 0, 0, 0, 1 };
        board.killCounts[3][0] = 2;
        board.killCounts[3][1] = 1;
        board.killCounts[0][3] = 1;
        Check(PointsFor(board, 3) == 3, "the lone player scores their kills");
        Check(PointsFor(board, 0) == 1, "the trio scores its kills, and not its deaths");
        Check(PointsFor(board, 1) == PointsFor(board, 2), "the trio shares a score");
    }
    // 2 vs 1: players 0 and 1 against 2.
    {
        ScoreBoard board = BoardFor(MpScenario::TwoVsOne, 3);
        board.teamOrToken = { 0, 0, 1, 0 };
        board.killCounts[0][2] = 2;
        board.killCounts[1][2] = 1;
        board.killCounts[2][1] = 4;
        Check(PointsFor(board, 0) == 3 && PointsFor(board, 1) == 3, "the pair shares its score");
        Check(PointsFor(board, 2) == 4, "the lone player scores against the pair");
    }
}

void TestRank() {
    // Normal: the order of the points, ties sharing the first place of them.
    for (int players = 2; players <= 4; players++) {
        ScoreBoard board = BoardFor(MpScenario::Normal, players);
        for (int player = 0; player < players; player++) {
            // Player p has p kills of player 0 (player 0 has none).
            board.killCounts[static_cast<size_t>(player)][0] = player;
        }
        board.killCounts[0][0] = 0;
        for (int player = 0; player < players; player++) {
            Check(RankOf(board, player) == players - 1 - player, "a rising score is a falling rank");
        }
    }

    // A tie shares the better place, and the next player is placed after both.
    {
        ScoreBoard board = BoardFor(MpScenario::Normal, 4);
        board.killCounts[1][0] = 3;
        board.killCounts[2][0] = 3;
        board.killCounts[3][0] = 1;
        board.killCounts[0][0] = 0;
        Check(RankOf(board, 1) == 0 && RankOf(board, 2) == 0, "players tied on top are both first");
        Check(RankOf(board, 3) == 2, "the next is third");
        Check(RankOf(board, 0) == 3, "the last is fourth");
    }
    // Everyone level is everyone first.
    for (int players = 2; players <= 4; players++) {
        const ScoreBoard board = BoardFor(MpScenario::Normal, players);
        for (int player = 0; player < players; player++) {
            Check(RankOf(board, player) == 0, "an even game is first for everyone");
        }
    }

    // Team scenarios read the places that cannot exist as second.
    {
        ScoreBoard board = BoardFor(MpScenario::TwoVsTwo, 4);
        board.teamOrToken = { 0, 1, 0, 1 };
        board.killCounts[0][1] = 3;
        // Team 0 scores 3 and team 1 scores -3: players 0 and 2 are first, 1
        // and 3 are tied for the first of the last two places, which is third,
        // read as second.
        Check(RankOf(board, 0) == 0 && RankOf(board, 2) == 0, "the winning pair is first");
        Check(RankOf(board, 1) == 1 && RankOf(board, 3) == 1, "the losing pair is second, not third");
    }
    {
        ScoreBoard board = BoardFor(MpScenario::ThreeVsOne, 4);
        board.teamOrToken = { 0, 0, 0, 1 };
        board.killCounts[3][0] = 1;
        // The lone player leads with 1; the trio shares -1 from the third place,
        // which the scenario reads as second.
        Check(RankOf(board, 3) == 0, "the lone player is first");
        Check(RankOf(board, 0) == 1 && RankOf(board, 1) == 1 && RankOf(board, 2) == 1, "the trio is second");
    }
    {
        ScoreBoard board = BoardFor(MpScenario::TwoVsOne, 3);
        board.teamOrToken = { 0, 0, 1, 0 };
        board.killCounts[0][2] = 1;
        Check(RankOf(board, 0) == 0 && RankOf(board, 1) == 0, "the pair is first");
        Check(RankOf(board, 2) == 1, "the lone player is second, not third");
    }
    // A tied team game: both teams first.
    {
        ScoreBoard board = BoardFor(MpScenario::TwoVsTwo, 4);
        board.teamOrToken = { 0, 0, 1, 1 };
        board.killCounts[0][2] = 2;
        board.killCounts[2][0] = 2;
        for (int player = 0; player < 4; player++) {
            Check(RankOf(board, player) == 0, "tied teams are all first");
        }
    }
    {
        ScoreBoard board = BoardFor(MpScenario::ThreeVsOne, 4);
        board.teamOrToken = { 0, 0, 0, 1 };
        board.killCounts[0][3] = 1;
        board.killCounts[3][0] = 1;
        for (int player = 0; player < 4; player++) {
            Check(RankOf(board, player) == 0, "a tied 3 vs 1 is all first");
        }
    }
    // Flag Tag ranks the flag counters.
    {
        ScoreBoard board = BoardFor(MpScenario::TheLivingDaylights, 3);
        board.flagCounter = { 10, 30, 20, 0 };
        Check(RankOf(board, 1) == 0 && RankOf(board, 2) == 1 && RankOf(board, 0) == 2, "flag counters set the rank");
    }
}

void TestDisplay() {
    Check(ShouldDisplayRank(MpScenario::Normal, true) && ShouldDisplayRank(MpScenario::TwoVsOne, false),
          "most scenarios always show a rank");
    Check(!ShouldDisplayRank(MpScenario::YouOnlyLiveTwice, true) &&
              ShouldDisplayRank(MpScenario::YouOnlyLiveTwice, false),
          "You Only Live Twice shows no rank on the game over page");
    Check(ShouldDisplayScore(MpScenario::Normal, false) && ShouldDisplayScore(MpScenario::TwoVsTwo, true),
          "the points scenarios show a score");
    Check(!ShouldDisplayScore(MpScenario::YouOnlyLiveTwice, false) &&
              !ShouldDisplayScore(MpScenario::TheLivingDaylights, false),
          "You Only Live Twice and Flag Tag show no score");
}

std::array<AwardInput, kMpPlayers> QuietInputs() {
    std::array<AwardInput, kMpPlayers> inputs{};
    for (AwardInput& input : inputs) {
        input.shotsTotal = 500;
        input.longestInning = 0;
        input.shortestInning = 500;
        input.bodyArmourPickups = 4.0f;
        input.timeOtherPlayersOnScreen = 1;
        input.damageToBackside = 0;
    }
    return inputs;
}

void TestMetrics() {
    for (int players = 2; players <= 4; players++) {
        std::array<AwardInput, kMpPlayers> inputs = QuietInputs();
        std::array<std::array<int32_t, kMpPlayers>, kMpPlayers> kills{};
        // Player 0 kills each other player three times, kills themselves once,
        // and fires 99 shots with 4 headshots.
        for (int victim = 1; victim < players; victim++) {
            kills[0][static_cast<size_t>(victim)] = 3;
        }
        kills[0][0] = 1;
        inputs[0].shotsTotal = 99;
        inputs[0].shotsHead = 4;
        inputs[0].distanceTraveled = 100000.0f * 6.0f;

        const std::array<AwardMetrics, kMpPlayers> metrics = BuildMetrics(players, inputs, kills, 3600 * 60 - 1);
        Check(metrics[0].kills == 3 * (players - 1), "kills exclude suicides");
        Check(metrics[0].suicides == 1, "suicides are the player's own entry");
        Check(metrics[0].deaths == 1, "the player's own kill is a death");
        for (int victim = 1; victim < players; victim++) {
            Check(metrics[static_cast<size_t>(victim)].deaths == 3, "each victim died three times");
            Check(metrics[static_cast<size_t>(victim)].kills == 0, "the victims killed nobody");
        }
        Check(metrics[0].shots == 99 && metrics[0].headshots == 4, "shots and headshots carry over");
        Check(std::abs(metrics[0].ksRatio - (3.0f * (players - 1) * 100.0f / 100.0f)) < 1e-3f,
              "kills per shot is a percentage over shots plus one");
        Check(std::abs(metrics[0].kdRatio - (3.0f * (players - 1) * 100.0f / 2.0f)) < 1e-3f,
              "kills per death is a percentage over deaths plus one");
        // 6 distance units of 100000 over exactly one hour.
        Check(std::abs(metrics[0].avgKmPerHour - 6.0f) < 1e-3f, "average speed over the game's hours");
        Check(metrics[0].awards == 0, "no awards before they are chosen");
    }
}

// The index of the first award a player has been given, or -1.
bool Has(const AwardMetrics& metrics, uint32_t award) {
    return (metrics.awards & award) != 0;
}

void TestEligibility() {
    for (int players = 2; players <= 4; players++) {
        std::array<AwardInput, kMpPlayers> inputs = QuietInputs();
        std::array<std::array<int32_t, kMpPlayers>, kMpPlayers> kills{};
        // Player 0: the most suicides, headshots and kills, the fewest shots, the
        // most frantic, and the highest armour count.
        kills[0][0] = 2;
        kills[0][1] = 5;
        inputs[0].shotsTotal = 50;
        inputs[0].shotsHead = 7;
        inputs[0].distanceTraveled = 100000.0f * 30.0f;
        inputs[0].bodyArmourPickups = 9.0f;
        inputs[0].longestInning = 4000;
        inputs[0].shortestInning = 30;
        inputs[0].mostKilledOneTime = 3;
        // The last player: no armour, the most damage to a backside, coward.
        const size_t last = static_cast<size_t>(players - 1);
        inputs[last].bodyArmourPickups = 0.0f;
        inputs[last].damageToBackside = 6;
        inputs[last].timeOtherPlayersOnScreen = 0;
        inputs[last].shortestInning = 900;

        Script none({});
        std::array<AwardMetrics, kMpPlayers> metrics = BuildMetrics(players, inputs, kills, 3600 * 60 - 1);
        ChooseEligibleAwards(players, metrics, inputs, none.Source());

        Check(Has(metrics[0], Award::MostSuicidal), "the player who killed themselves is the Lemming");
        Check(Has(metrics[0], Award::WhoNeedsAmmo), "under a hundred shots is Who Needs Ammo");
        Check(Has(metrics[0], Award::AcNegative10), "more than six armour pickups is AC -10");
        Check(Has(metrics[0], Award::Marksmanship), "the most headshots is Marksmanship");
        Check(Has(metrics[0], Award::MostProfessional), "kills per shot");
        Check(Has(metrics[0], Award::MostDeadly), "kills per death");
        Check(Has(metrics[0], Award::MostFrantic), "average speed over ten");
        Check(Has(metrics[0], Award::LongestInnings), "the longest life");
        Check(Has(metrics[0], Award::TripleKill), "a streak of three");
        Check(Has(metrics[last], Award::WheresTheArmour), "no armour is Where's the Armour");
        Check(Has(metrics[last], Award::MostCowardly), "the least time with others on screen is the coward");
        Check(Has(metrics[last], Award::MostHarmless) || players > 2, "no kills makes the harmless one");
        Check(!Has(metrics[last], Award::MostHonorable), "damage to a backside is not honorable");
        Check(Has(metrics[last], Award::MostDishonorable), "the most damage to a backside is dishonorable");
        Check(!Has(metrics[0], Award::DoubleKill) && !Has(metrics[0], Award::QuadKill),
              "only the streak that was made");
        Check(Has(metrics[0], Award::ShortestInnings), "the shortest life belongs to the shortest");
    }

    // Streaks of two and four.
    {
        std::array<AwardInput, kMpPlayers> inputs = QuietInputs();
        inputs[0].mostKilledOneTime = 2;
        inputs[1].mostKilledOneTime = 4;
        Script none({});
        std::array<AwardMetrics, kMpPlayers> metrics =
            BuildMetrics(2, inputs, std::array<std::array<int32_t, kMpPlayers>, kMpPlayers>{}, 100);
        ChooseEligibleAwards(2, metrics, inputs, none.Source());
        Check(Has(metrics[0], Award::DoubleKill) && Has(metrics[1], Award::QuadKill), "double and quad kills");
    }

    // Nothing earned: the quiet game still names a most honorable, a coward and
    // a harmless player, so everyone is eligible for something only by ties.
    {
        std::array<AwardInput, kMpPlayers> inputs = QuietInputs();
        Script even({ 0 });
        std::array<AwardMetrics, kMpPlayers> metrics =
            BuildMetrics(4, inputs, std::array<std::array<int32_t, kMpPlayers>, kMpPlayers>{}, 100);
        ChooseEligibleAwards(4, metrics, inputs, even.Source());
        Check(Has(metrics[0], Award::MostHonorable) && Has(metrics[0], Award::MostCowardly) &&
                  Has(metrics[0], Award::MostHarmless),
              "ties go to the first player on even draws");
        Check(metrics[1].awards == 0 && metrics[2].awards == 0 && metrics[3].awards == 0, "and the rest get none");
    }
}

void TestPick() {
    // Nothing eligible: nobody is shown an award, and the first player draws
    // twice (the first loop's draw after it ends, then the second loop's).
    {
        std::array<AwardMetrics, kMpPlayers> metrics{};
        Script script({ 3, 3 });
        const std::array<AwardPick, kMpPlayers> picks = PickAwards(2, metrics, script.Source());
        Check(picks[0].first == -1 && picks[0].second == -1 && picks[1].first == -1, "no awards to show");
        Check(script.Draws() == 4, "each player draws two numbers when there is nothing to pick");
    }
    // A quad kill is always shown first, without a draw before it.
    {
        std::array<AwardMetrics, kMpPlayers> metrics{};
        metrics[0].awards = Award::QuadKill | Award::MostDeadly | Award::Marksmanship;
        Script script({ 6, 4 });
        const std::array<AwardPick, kMpPlayers> picks = PickAwards(1, metrics, script.Source());
        Check(picks[0].first == 16, "the quad kill is first");
        // The first loop drew 6 for its next index but stops (awards remain,
        // done == 1). The second loop starts with the next draw, 4: Marksmanship.
        Check(picks[0].second == 4, "the second award comes from the next draws");
        Check(metrics[0].awards == Award::MostDeadly, "the awards given are taken off");
    }
    // Without a quad kill the first award is found by drawing.
    {
        std::array<AwardMetrics, kMpPlayers> metrics{};
        metrics[0].awards = Award::MostDeadly | Award::MostFrantic | Award::Marksmanship;
        // The quad kill index fails, then 6 (Most Deadly) hits; the second loop
        // draws 17 % 17 = 0 (none), then 9 (Most Frantic).
        Script script({ 6, 0, 17, 9 });
        const std::array<AwardPick, kMpPlayers> picks = PickAwards(1, metrics, script.Source());
        Check(picks[0].first == 6, "the first award is the first drawn that the player has");
        Check(picks[0].second == 9, "the second skips draws the player lacks");
    }
    // A single award leaves the second empty.
    {
        std::array<AwardMetrics, kMpPlayers> metrics{};
        metrics[0].awards = Award::MostDeadly;
        Script script({ 6, 9 });
        const std::array<AwardPick, kMpPlayers> picks = PickAwards(1, metrics, script.Source());
        Check(picks[0].first == 6 && picks[0].second == -1, "one award is shown alone");
    }
    // Every player is picked for, in order, for 2, 3 and 4 players.
    for (int players = 2; players <= 4; players++) {
        std::array<AwardMetrics, kMpPlayers> metrics{};
        for (int player = 0; player < players; player++) {
            metrics[static_cast<size_t>(player)].awards = Award::QuadKill;
        }
        Script script({ 0 });
        const std::array<AwardPick, kMpPlayers> picks = PickAwards(players, metrics, script.Source());
        for (int player = 0; player < players; player++) {
            Check(picks[static_cast<size_t>(player)].first == 16, "each player is given their quad kill");
        }
    }
}

} // namespace

int main() {
    TestFindInt();
    TestFindFloat();
    TestNormalPoints();
    TestYoltAndFlagPoints();
    TestTeamPoints();
    TestRank();
    TestDisplay();
    TestMetrics();
    TestEligibility();
    TestPick();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("all multiplayer scoring checks passed\n");
    return 0;
}
