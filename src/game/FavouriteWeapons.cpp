#include "game/FavouriteWeapons.h"

namespace Frigate::Game {

void FavouriteWeapons::Store(int player, int32_t right, int32_t left) {
    if (player < 0 || player >= kPlayers) {
        return;
    }
    weapons_[player][kRight] = right;
    weapons_[player][kLeft] = left;
}

void FavouriteWeapons::StoreForEveryPlayer(int playerCount,
                                           const std::function<std::array<int32_t, 2>(int)>& weaponOfChoice) {
    for (int player = 0; player < playerCount && player < kPlayers; player++) {
        const std::array<int32_t, 2> pair = weaponOfChoice(player);
        Store(player, pair[kRight], pair[kLeft]);
    }
}

int32_t FavouriteWeapons::Weapon(int player, int hand) const {
    if (player < 0 || player >= kPlayers || hand < kRight || hand > kLeft) {
        return 0;
    }
    return weapons_[player][hand];
}

bool FavouriteWeapons::IsDualWielded(int player) const {
    return Weapon(player, kRight) > 0 && Weapon(player, kLeft) == Weapon(player, kRight);
}

} // namespace Frigate::Game
