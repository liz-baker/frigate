#include "level/PlayerOrder.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

// A random source that hands back `draws` in turn and counts them.
struct Scripted {
    std::vector<uint32_t> draws;
    size_t next = 0;
    uint32_t operator()() {
        return draws[next++ % draws.size()];
    }
};

PlayerOrder Shuffled(std::vector<uint32_t> draws, size_t* used = nullptr) {
    Scripted script{ std::move(draws) };
    PlayerOrder order;
    order.Shuffle([&script] { return script(); });
    if (used != nullptr) {
        *used = script.next;
    }
    return order;
}

void TestIdentity() {
    PlayerOrder order;
    for (int count = 1; count <= 4; count++) {
        for (int n = 0; n < count; n++) {
            Check(order.Nth(n, count) == n, "an unshuffled order visits the players in turn");
            Check(order.PositionOf(n, count) == n, "an unshuffled order puts player n at position n");
        }
    }
}

void TestShuffleDraws() {
    size_t used = 0;
    const PlayerOrder zero = Shuffled({ 0 }, &used);
    Check(used == 3, "a shuffle draws three random numbers");
    Check(zero.Ids() == std::array<int, 4>({ 0, 1, 2, 3 }), "draws that land on themselves leave the order alone");

    const PlayerOrder ones = Shuffled({ 1, 1, 1 });
    Check(ones.Ids() == std::array<int, 4>({ 1, 2, 3, 0 }), "each draw swaps its slot with the next");

    const PlayerOrder far = Shuffled({ 3, 2, 1 });
    // i=0 swaps with 3: [3,1,2,0]; i=1 swaps with 1+2%3=3: [3,0,2,1]; i=2 swaps with 2+1%2=3: [3,0,1,2].
    Check(far.Ids() == std::array<int, 4>({ 3, 0, 1, 2 }), "a draw swaps with the slot its remainder names");

    // A later shuffle starts from the identity, not from the last order.
    PlayerOrder order = ones;
    Scripted again{ { 0 } };
    order.Shuffle([&again] { return again(); });
    Check(order.Ids() == std::array<int, 4>({ 0, 1, 2, 3 }), "each shuffle starts from the players in turn");
}

void TestNthAndPosition() {
    const PlayerOrder order = Shuffled({ 1, 1, 1 }); // 1, 2, 3, 0
    // Two players: 1 then 0.
    Check(order.Nth(0, 2) == 1 && order.Nth(1, 2) == 0, "two players keep the shuffle's order among themselves");
    Check(order.PositionOf(1, 2) == 0 && order.PositionOf(0, 2) == 1, "positions among two players");
    // Three players: 1, 2, 0.
    Check(order.Nth(0, 3) == 1 && order.Nth(1, 3) == 2 && order.Nth(2, 3) == 0, "three players");
    Check(order.PositionOf(2, 3) == 1 && order.PositionOf(0, 3) == 2, "positions among three players");
    // Four players: 1, 2, 3, 0.
    Check(order.Nth(2, 4) == 3 && order.Nth(3, 4) == 0, "four players");
    Check(order.PositionOf(3, 4) == 2 && order.PositionOf(0, 4) == 3, "positions among four players");
    // One player is always first.
    Check(order.Nth(0, 1) == 0 && order.PositionOf(0, 1) == 0, "a lone player moves first");
    Check(order.Nth(5, 2) == 0, "a place nobody holds reads as player 0");
}

void TestEveryPlayerOnce() {
    uint32_t seed = 12345;
    PlayerOrder order;
    for (int round = 0; round < 2000; round++) {
        order.Shuffle([&seed] {
            seed = (seed * 1664525u) + 1013904223u;
            return seed >> 8;
        });
        for (int count = 1; count <= 4; count++) {
            std::array<int, 4> seen{};
            for (int n = 0; n < count; n++) {
                const int player = order.Nth(n, count);
                Check(player >= 0 && player < count, "the nth player is one of the players");
                seen[static_cast<size_t>(player)]++;
                Check(order.PositionOf(player, count) == n, "a player's position is the n they are nth at");
            }
            for (int player = 0; player < count; player++) {
                Check(seen[static_cast<size_t>(player)] == 1, "every player moves exactly once");
            }
        }
    }
}

} // namespace

int main() {
    TestIdentity();
    TestShuffleDraws();
    TestNthAndPosition();
    TestEveryPlayerOnce();
    if (gFailures == 0) {
        std::printf("all player order tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
