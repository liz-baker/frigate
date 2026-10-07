#include "level/YoltRound.h"

#include <array>
#include <cstdio>

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

using Players = std::array<YoltPlayer, 4>;
using Order = std::array<int, 4>;

void Kill(Players& players, int victim, bool fullyDead = false) {
    players[static_cast<size_t>(victim)].alive = false;
    players[static_cast<size_t>(victim)].killedCount = 2;
    players[static_cast<size_t>(victim)].fullyDead = fullyDead;
}

void TestOnlyKilledTwiceIsOut() {
    for (int count = 2; count <= 4; count++) {
        Players players;
        Order order{};
        players[0].killedCount = 1;
        players[0].alive = false;
        Check(TickYoltRound(count, players, order) == YoltOutcome::Continue, "a player killed once is not out");
        Check(order[0] == 0, "and has no place in the order");
    }
}

void TestOrderOut() {
    // The order out is the players alive that tick plus one: the first out of
    // four is placed 4th, the next 3rd.
    Players players;
    Order order{};
    Kill(players, 2);
    Check(TickYoltRound(4, players, order) == YoltOutcome::Continue, "one of four out carries on");
    Check(order[2] == 4, "the first of four out is placed fourth");
    Kill(players, 0);
    Check(TickYoltRound(4, players, order) == YoltOutcome::Continue, "two of four out carries on");
    Check(order[0] == 3 && order[2] == 4, "the second out is third and the first keeps its place");
    TickYoltRound(4, players, order);
    Check(order[0] == 3, "a place is kept once given");
    Kill(players, 1);
    Check(TickYoltRound(4, players, order) == YoltOutcome::StopPlay, "three of four out stops play");
    Check(order[1] == 2, "the third out is second");
}

void TestEnd() {
    for (int count = 2; count <= 4; count++) {
        Players players;
        Order order{};
        for (int victim = 0; victim < count - 1; victim++) {
            Kill(players, victim);
        }
        Check(TickYoltRound(count, players, order) == YoltOutcome::StopPlay,
              "all but one out stops play while they die");
        for (int victim = 0; victim < count - 2; victim++) {
            players[static_cast<size_t>(victim)].fullyDead = true;
        }
        Check(TickYoltRound(count, players, order) == YoltOutcome::StopPlay,
              "the game ends only when all but one have finished dying");
        players[static_cast<size_t>(count - 2)].fullyDead = true;
        Check(TickYoltRound(count, players, order) == YoltOutcome::End, "the last of them finishing ends the game");
    }
}

void TestCountOnly() {
    // Players past the count are not read.
    Players players;
    Order order{};
    Kill(players, 3);
    Check(TickYoltRound(3, players, order) == YoltOutcome::Continue && order[3] == 0,
          "a player past the count is ignored");
}

} // namespace

int main() {
    TestOnlyKilledTwiceIsOut();
    TestOrderOut();
    TestEnd();
    TestCountOnly();
    return gFailures == 0 ? 0 : 1;
}
