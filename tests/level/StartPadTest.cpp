#include "level/IntroSection.h"

#include <cstdio>
#include <optional>
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

// Pads 2000 units apart along x.
std::vector<Vec3> Pads(size_t count) {
    std::vector<Vec3> pads;
    for (size_t i = 0; i < count; i++) {
        pads.push_back({ static_cast<float>(i) * 2000.0f, 0.0f, 0.0f });
    }
    return pads;
}

struct Draws {
    uint32_t value = 0;
    int count = 0;
    uint32_t operator()() {
        count++;
        return value;
    }
};

size_t Choose(const std::vector<Vec3>& pads, int player, const std::vector<std::optional<Vec3>>& others, int* cursor,
              Draws* draws) {
    return ChooseStartPad(pads, player, others, cursor, [draws] { return (*draws)(); });
}

void TestFirstPlayerTakesTheNextPad() {
    const std::vector<Vec3> pads = Pads(5);
    std::vector<std::optional<Vec3>> others(4);
    int cursor = 0;
    Draws draws;
    Check(Choose(pads, 0, others, &cursor, &draws) == 1, "the cursor steps before it is used");
    Check(cursor == 1 && draws.count == 0, "a clear pad takes no random draw");
    cursor = 7;
    Check(Choose(pads, 0, others, &cursor, &draws) == 3, "the cursor wraps round the pads");
}

void TestSkipsPadsNearOthers() {
    const std::vector<Vec3> pads = Pads(5);
    std::vector<std::optional<Vec3>> others(4);
    others[0] = pads[1];
    int cursor = 0;
    Draws draws;
    // Pad 1 holds player 0, so player 1 moves on to pad 2.
    Check(Choose(pads, 1, others, &cursor, &draws) == 2, "a pad with another player on it is skipped");
    Check(cursor == 2 && draws.count == 0, "the cursor advanced past the skipped pad");

    // 999 units away is too near, 1000 is not.
    others[0] = Vec3{ pads[1][0] + 999.0f, 0.0f, 0.0f };
    cursor = 0;
    Check(Choose(pads, 1, others, &cursor, &draws) == 2, "a player within 1000 units blocks the pad");
    others[0] = Vec3{ pads[1][0] + 1000.0f, 0.0f, 0.0f };
    cursor = 0;
    Check(Choose(pads, 1, others, &cursor, &draws) == 1, "a player 1000 units away does not");
}

void TestIgnoresSelfAndAbsentPlayers() {
    const std::vector<Vec3> pads = Pads(3);
    std::vector<std::optional<Vec3>> others(4);
    others[2] = pads[1];
    int cursor = 0;
    Draws draws;
    Check(Choose(pads, 2, others, &cursor, &draws) == 1, "a player is no obstacle to themselves");
    others[2].reset();
    others[3].reset();
    cursor = 0;
    Check(Choose(pads, 0, others, &cursor, &draws) == 1, "players without a prop yet are no obstacle");
}

void TestNoClearPadPicksAtRandom() {
    const std::vector<Vec3> pads = Pads(3);
    std::vector<std::optional<Vec3>> others(4);
    // The other player stands among every pad's reach: a single pad 500 units
    // from each of three pads 2000 apart can't, so use three others spread out.
    others[1] = pads[0];
    others[2] = pads[1];
    others[3] = pads[2];
    int cursor = 0;
    Draws draws{ 7, 0 };
    const size_t pad = Choose(pads, 0, others, &cursor, &draws);
    Check(draws.count == 1, "one random draw when every pad is taken");
    Check(pad == 7 % 3, "the draw picks a pad modulo the pad count");
    Check(cursor == 3, "every pad was tried first");
}

void TestFourPlayersSpreadOut() {
    const std::vector<Vec3> pads = Pads(8);
    std::vector<std::optional<Vec3>> placed(4);
    int cursors[4] = { 10, 11, 12, 13 };
    std::vector<size_t> taken;
    for (int player = 0; player < 4; player++) {
        Draws draws;
        const size_t pad = Choose(pads, player, placed, &cursors[player], &draws);
        Check(draws.count == 0, "eight pads leave room for four players");
        for (const size_t other : taken) {
            Check(other != pad, "no two players share a pad");
        }
        taken.push_back(pad);
        placed[static_cast<size_t>(player)] = pads[pad];
    }
}

} // namespace

int main() {
    TestFirstPlayerTakesTheNextPad();
    TestSkipsPadsNearOthers();
    TestIgnoresSelfAndAbsentPlayers();
    TestNoClearPadPicksAtRandom();
    TestFourPlayersSpreadOut();
    if (gFailures == 0) {
        std::printf("all start pad tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
