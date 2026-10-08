#include "level/PlayerOrder.h"

#include <utility>

namespace Frigate::Level {

void PlayerOrder::Shuffle(const std::function<uint32_t()>& random) {
    Reset();
    for (size_t i = 0; i < 3; i++) {
        const uint32_t draw = random();
        const size_t swapWith = i + (draw % static_cast<uint32_t>(kSlots - static_cast<int>(i)));
        std::swap(ids_[i], ids_[swapWith]);
    }
}

int PlayerOrder::Nth(int n, int playerCount) const {
    for (const int player : ids_) {
        if (player < playerCount) {
            if (n == 0) {
                return player;
            }
            n--;
        }
    }
    return 0;
}

int PlayerOrder::PositionOf(int player, int playerCount) const {
    int position = 0;
    for (const int other : ids_) {
        if (other == player) {
            break;
        }
        if (other < playerCount) {
            position++;
        }
    }
    return position;
}

} // namespace Frigate::Level
