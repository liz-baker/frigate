#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace Frigate::Game {

// array_favweapon: each player's weapon of choice (the right and left hand
// items held longest, from PlayerWeapons::WeaponOfChoice) as the last game
// left it. It is held for the whole run, never saved, and starts out as
// ITEM_UNARMED (0) for every player.
class FavouriteWeapons {
  public:
    static constexpr int kPlayers = 4;
    static constexpr int kRight = 0;
    static constexpr int kLeft = 1;

    // store_favorite_weapon_current_player, for `player`.
    void Store(int player, int32_t right, int32_t left);
    // set_favorite_weapon_for_every_player, run as a stage unloads: stores
    // `weaponOfChoice(player)` (bondinvGetWeaponOfChoice for that player) for
    // each of the `playerCount` players.
    void StoreForEveryPlayer(int playerCount, const std::function<std::array<int32_t, 2>(int)>& weaponOfChoice);

    // array_favweapon[player][hand]; 0 for a player or hand out of range.
    int32_t Weapon(int player, int hand) const;
    // Whether the player's two hands hold the same real weapon, which the
    // stats page writes as "name x 2".
    bool IsDualWielded(int player) const;

  private:
    std::array<std::array<int32_t, 2>, kPlayers> weapons_{};
};

} // namespace Frigate::Game
