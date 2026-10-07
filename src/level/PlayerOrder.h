#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace Frigate::Level {

// array_PLAYER_IDs: the order the stage's per-player ticks and render passes
// visit the players in, reshuffled once a frame. The shuffle always runs over
// the four slots, so a frame draws the same random numbers whatever the number
// of players.
class PlayerOrder {
  public:
    static constexpr int kSlots = 4;

    // reset_play_data_ptrs: the players in their own order.
    void Reset() {
        ids_ = { 0, 1, 2, 3 };
    }

    // shuffle_player_ids: three draws of `random`.
    void Shuffle(const std::function<uint32_t()>& random);

    // get_nth_player_from_shuffled: the player that comes nth among the first
    // `playerCount` players; 0 when there is no such.
    int Nth(int n, int playerCount) const;
    // get_player_position_in_shuffled: how many of the first `playerCount`
    // players come before `player`.
    int PositionOf(int player, int playerCount) const;

    const std::array<int, kSlots>& Ids() const {
        return ids_;
    }

  private:
    std::array<int, kSlots> ids_{ 0, 1, 2, 3 };
};

} // namespace Frigate::Level
