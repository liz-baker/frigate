#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frigate::Level {

// What You Only Live Twice reads of a player each tick.
struct YoltPlayer {
    // bonddead == 0.
    bool alive = true;
    // The times the other players (and the player) have killed this player:
    // the sum of every player's kill_counts for them.
    int killedCount = 0;
    // The blood, the death animation and the fade to black are all over.
    bool fullyDead = false;
};

// What the round does after the tick.
enum class YoltOutcome {
    Continue,
    // Enough players are out that play stops while the last ones finish dying
    // (mpwatchSetStopPlayFlag).
    StopPlay,
    // Every player but one has finished dying: the game ends (mpCalculateAwards).
    End,
};

// lvlTick's You Only Live Twice tracking. A player killed twice is out; the
// order_out_in_yolt a player gets the first tick they are out is the number of
// players still alive that tick plus one, and `orderOut` keeps it. The round
// ends once all but one player are out.
template <size_t N>
YoltOutcome TickYoltRound(int playerCount, const std::array<YoltPlayer, N>& players, std::array<int, N>& orderOut) {
    int killedTotal = 0;
    int fullyDeadTotal = 0;
    int notDead = 0;
    for (int i = 0; i < playerCount; i++) {
        if (players[static_cast<size_t>(i)].alive) {
            notDead++;
        }
    }
    for (int i = 0; i < playerCount; i++) {
        const YoltPlayer& player = players[static_cast<size_t>(i)];
        if (player.killedCount >= 2) {
            if (orderOut[static_cast<size_t>(i)] == 0) {
                orderOut[static_cast<size_t>(i)] = static_cast<uint8_t>(notDead + 1);
            }
            killedTotal++;
            if (player.fullyDead) {
                fullyDeadTotal++;
            }
        }
    }
    if (fullyDeadTotal >= playerCount - 1) {
        return YoltOutcome::End;
    }
    if (killedTotal >= playerCount - 1) {
        return YoltOutcome::StopPlay;
    }
    return YoltOutcome::Continue;
}

} // namespace Frigate::Level
