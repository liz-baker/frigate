#include "level/MultiplayerScoring.h"

#include <algorithm>

namespace Frigate::Level {

namespace {

constexpr float kTicksPerHour = 3600.0f * 60.0f;
constexpr float kUnitsPerKilometre = 100000.0f;

// Whether a value takes the lead: strictly, or on a tie when a random bit says
// so. Only a tie draws a number.
bool Takes(bool strictly, bool tied, const MpRandom& random) {
    return strictly || (tied && (random() & 1) != 0);
}

} // namespace

int FindMaxInt(int players, const std::array<int32_t, kMpPlayers>& values, const MpRandom& random) {
    int32_t best;
    int result;
    if (Takes(values[0] < values[1], values[1] == values[0], random)) {
        result = 1;
        best = values[1];
    } else {
        result = 0;
        best = values[0];
    }
    if (players >= 3) {
        if (Takes(best < values[2], values[2] == best, random)) {
            result = 2;
            best = values[2];
        }
        if (players >= 4 && Takes(best < values[3], values[3] == best, random)) {
            result = 3;
        }
    }
    return result;
}

int FindMinInt(int players, const std::array<int32_t, kMpPlayers>& values, const MpRandom& random) {
    int32_t best;
    int result;
    if (Takes(values[1] < values[0], values[1] == values[0], random)) {
        result = 1;
        best = values[1];
    } else {
        result = 0;
        best = values[0];
    }
    if (players >= 3) {
        if (Takes(values[2] < best, values[2] == best, random)) {
            result = 2;
            best = values[2];
        }
        if (players >= 4 && Takes(values[3] < best, values[3] == best, random)) {
            result = 3;
        }
    }
    return result;
}

int FindMaxFloat(int players, const std::array<float, kMpPlayers>& values, const MpRandom& random) {
    int32_t best;
    int result;
    if (Takes(values[0] < values[1], values[1] == values[0], random)) {
        best = static_cast<int32_t>(values[1]);
        result = 1;
    } else {
        best = static_cast<int32_t>(values[0]);
        result = 0;
    }
    if (players >= 3) {
        if (Takes(static_cast<float>(best) < values[2], values[2] == static_cast<float>(best), random)) {
            best = static_cast<int32_t>(values[2]);
            result = 2;
        }
        if (players >= 4 &&
            Takes(static_cast<float>(best) < values[3], values[3] == static_cast<float>(best), random)) {
            result = 3;
        }
    }
    return result;
}

int FindMinFloat(int players, const std::array<float, kMpPlayers>& values, const MpRandom& random) {
    int32_t best;
    int result;
    if (Takes(values[1] < values[0], values[1] == values[0], random)) {
        best = static_cast<int32_t>(values[1]);
        result = 1;
    } else {
        best = static_cast<int32_t>(values[0]);
        result = 0;
    }
    if (players >= 3) {
        if (Takes(values[2] < static_cast<float>(best), values[2] == static_cast<float>(best), random)) {
            best = static_cast<int32_t>(values[2]);
            result = 2;
        }
        if (players >= 4 &&
            Takes(values[3] < static_cast<float>(best), values[3] == static_cast<float>(best), random)) {
            result = 3;
        }
    }
    return result;
}

int32_t PointsFor(const ScoreBoard& board, int player) {
    const size_t self = static_cast<size_t>(player);
    // The team scenarios read the token field as the player's team.
    const int32_t teamOrToken = board.teamOrToken[self];
    int32_t points = 0;

    switch (board.scenario) {
        case Game::MpScenario::Normal:
        case Game::MpScenario::TheManWithTheGoldenGun:
        case Game::MpScenario::LicenceToKill:
            for (int other = 0; other < board.players; other++) {
                if (other != player) {
                    points += board.killCounts[self][static_cast<size_t>(other)];
                } else {
                    points -= board.killCounts[static_cast<size_t>(other)][self];
                }
            }
            points += board.killedGoldenGunOwner[self] * (board.players - 2);
            break;
        case Game::MpScenario::YouOnlyLiveTwice:
            points = kMpPlayers - board.orderOutInYolt[self];
            break;
        case Game::MpScenario::TheLivingDaylights:
            points = board.flagCounter[self];
            break;
        case Game::MpScenario::TwoVsTwo:
        case Game::MpScenario::ThreeVsOne:
        case Game::MpScenario::TwoVsOne:
            for (int member = 0; member < board.players; member++) {
                if (board.teamOrToken[static_cast<size_t>(member)] != teamOrToken) {
                    continue;
                }
                for (int other = 0; other < board.players; other++) {
                    const int32_t kills = board.killCounts[static_cast<size_t>(member)][static_cast<size_t>(other)];
                    if (board.teamOrToken[static_cast<size_t>(other)] != teamOrToken) {
                        points += kills;
                    } else {
                        points -= kills;
                    }
                }
            }
            break;
        default:
            break;
    }
    return points;
}

int RankOf(const ScoreBoard& board, int player) {
    const int count = board.players;
    std::array<int32_t, kMpPlayers> scores{};
    std::array<int, kMpPlayers> order{};
    for (int i = 0; i < count; i++) {
        scores[static_cast<size_t>(i)] = PointsFor(board, i);
        order[static_cast<size_t>(i)] = i;
    }
    for (int pass = 0; pass < count; pass++) {
        for (int i = 0; i < count - 1; i++) {
            const size_t at = static_cast<size_t>(i);
            if (scores[at] < scores[at + 1]) {
                std::swap(scores[at], scores[at + 1]);
                std::swap(order[at], order[at + 1]);
            }
        }
    }

    int place = 0;
    while (place < count && order[static_cast<size_t>(place)] != player) {
        place++;
    }
    if (place == count) {
        return 0;
    }
    int rank = 0;
    while (rank <= place && scores[static_cast<size_t>(rank)] != scores[static_cast<size_t>(place)]) {
        rank++;
    }

    switch (rank) {
        case 2:
            if (board.scenario == Game::MpScenario::TwoVsTwo || board.scenario == Game::MpScenario::TwoVsOne) {
                return 1;
            }
            return 2;
        case 3:
            if (board.scenario == Game::MpScenario::ThreeVsOne) {
                return 1;
            }
            return 3;
        default:
            return rank;
    }
}

bool ShouldDisplayRank(Game::MpScenario scenario, bool param) {
    return scenario == Game::MpScenario::YouOnlyLiveTwice ? !param : true;
}

bool ShouldDisplayScore(Game::MpScenario scenario, bool) {
    return scenario != Game::MpScenario::YouOnlyLiveTwice && scenario != Game::MpScenario::TheLivingDaylights;
}

std::array<AwardMetrics, kMpPlayers> BuildMetrics(int players, const std::array<AwardInput, kMpPlayers>& inputs,
                                                  const std::array<std::array<int32_t, kMpPlayers>, kMpPlayers>& kills,
                                                  int32_t duration) {
    std::array<AwardMetrics, kMpPlayers> metrics{};
    for (int i = 0; i < players; i++) {
        const size_t at = static_cast<size_t>(i);
        const AwardInput& input = inputs[at];
        AwardMetrics& m = metrics[at];
        m.shots = input.shotsTotal;
        m.headshots = input.shotsHead;
        for (int j = 0; j < players; j++) {
            m.deaths += kills[static_cast<size_t>(j)][at];
            if (i == j) {
                m.suicides += kills[at][static_cast<size_t>(j)];
            } else {
                m.kills += kills[at][static_cast<size_t>(j)];
            }
        }
        m.ksRatio = static_cast<float>(m.kills) * 100.0f / (static_cast<float>(m.shots) + 1.0f);
        m.kdRatio = static_cast<float>(m.kills) * 100.0f / (static_cast<float>(m.deaths) + 1.0f);
        m.damageToBackside = input.damageToBackside;
        m.timeOtherPlayersOnScreen = input.timeOtherPlayersOnScreen;
        m.avgKmPerHour =
            input.distanceTraveled / kUnitsPerKilometre / (static_cast<float>(duration + 1) / kTicksPerHour);
        m.bodyArmourPickups = input.bodyArmourPickups;
        m.awards = 0;
        m.longestInning = input.longestInning;
        m.shortestInning = input.shortestInning;
    }
    return metrics;
}

void ChooseEligibleAwards(int players, std::array<AwardMetrics, kMpPlayers>& metrics,
                          const std::array<AwardInput, kMpPlayers>& inputs, const MpRandom& random) {
    const auto ints = [&](auto field) {
        std::array<int32_t, kMpPlayers> values{};
        for (size_t i = 0; i < kMpPlayers; i++) {
            values[i] = metrics[i].*field;
        }
        return values;
    };
    const auto floats = [&](auto field) {
        std::array<float, kMpPlayers> values{};
        for (size_t i = 0; i < kMpPlayers; i++) {
            values[i] = metrics[i].*field;
        }
        return values;
    };
    const auto at = [&](int index) -> AwardMetrics& { return metrics[static_cast<size_t>(index)]; };

    int i = FindMaxInt(players, ints(&AwardMetrics::suicides), random);
    if (at(i).suicides > 0) {
        at(i).awards |= Award::MostSuicidal;
    }

    i = FindMinInt(players, ints(&AwardMetrics::shots), random);
    if (at(i).shots < 100) {
        at(i).awards |= Award::WhoNeedsAmmo;
    }

    i = FindMinFloat(players, floats(&AwardMetrics::bodyArmourPickups), random);
    if (at(i).bodyArmourPickups <= 2.0f) {
        at(i).awards |= Award::WheresTheArmour;
    }

    i = FindMaxFloat(players, floats(&AwardMetrics::bodyArmourPickups), random);
    if (at(i).bodyArmourPickups > 6.0f) {
        at(i).awards |= Award::AcNegative10;
    }

    i = FindMaxInt(players, ints(&AwardMetrics::headshots), random);
    if (at(i).headshots > 0) {
        at(i).awards |= Award::Marksmanship;
    }

    i = FindMaxFloat(players, floats(&AwardMetrics::ksRatio), random);
    if (at(i).ksRatio > 0.0f) {
        at(i).awards |= Award::MostProfessional;
    }

    i = FindMaxFloat(players, floats(&AwardMetrics::kdRatio), random);
    if (at(i).kdRatio > 0.0f) {
        at(i).awards |= Award::MostDeadly;
    }

    i = FindMinFloat(players, floats(&AwardMetrics::kdRatio), random);
    at(i).awards |= Award::MostHarmless;

    i = FindMinInt(players, ints(&AwardMetrics::timeOtherPlayersOnScreen), random);
    at(i).awards |= Award::MostCowardly;

    i = FindMaxFloat(players, floats(&AwardMetrics::avgKmPerHour), random);
    if (at(i).avgKmPerHour > 10.0f) {
        at(i).awards |= Award::MostFrantic;
    }

    i = FindMinInt(players, ints(&AwardMetrics::damageToBackside), random);
    at(i).awards |= Award::MostHonorable;

    i = FindMaxInt(players, ints(&AwardMetrics::damageToBackside), random);
    if (at(i).damageToBackside > 0 && (at(i).awards & Award::MostHonorable) == 0) {
        at(i).awards |= Award::MostDishonorable;
    }

    i = FindMaxInt(players, ints(&AwardMetrics::longestInning), random);
    if (at(i).longestInning > 0) {
        at(i).awards |= Award::LongestInnings;
    }

    i = FindMinInt(players, ints(&AwardMetrics::shortestInning), random);
    if (at(i).shortestInning > 0) {
        at(i).awards |= Award::ShortestInnings;
    }

    for (int player = 0; player < players; player++) {
        switch (inputs[static_cast<size_t>(player)].mostKilledOneTime) {
            case 4:
                at(player).awards |= Award::QuadKill;
                break;
            case 3:
                at(player).awards |= Award::TripleKill;
                break;
            case 2:
                at(player).awards |= Award::DoubleKill;
                break;
            default:
                break;
        }
    }
}

std::array<AwardPick, kMpPlayers> PickAwards(int players, std::array<AwardMetrics, kMpPlayers>& metrics,
                                             const MpRandom& random) {
    std::array<AwardPick, kMpPlayers> picks{};
    for (int player = 0; player < players; player++) {
        AwardMetrics& m = metrics[static_cast<size_t>(player)];
        AwardPick& pick = picks[static_cast<size_t>(player)];
        int done = 0;
        int index = kAwardCount - 1;

        while (done == 0) {
            if ((m.awards & (1u << index)) != 0) {
                m.awards &= ~(1u << index);
                pick.first = index;
                done = 1;
            }
            if (m.awards == 0) {
                done = 1;
            }
            index = static_cast<int>(random() % kAwardCount);
        }

        while (done < 2) {
            index = static_cast<int>(random() % kAwardCount);
            if ((m.awards & (1u << index)) != 0) {
                m.awards &= ~(1u << index);
                pick.second = index;
                done = 2;
            }
            if (m.awards == 0) {
                done = 2;
            }
        }
    }
    return picks;
}

} // namespace Frigate::Level
