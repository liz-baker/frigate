#include "level/Stage.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

bool Stage::AliveWithFlag() const {
    // bondinvIsAliveWithFlag.
    return !Cur().health.IsDead() && Cur().intro.inventory.HasItem(kItemToken);
}

bool Stage::HasGoldenGun() const {
    return Cur().intro.inventory.HasItem(kItemGoldenGun);
}

bool Stage::ScenarioRefusesArmour() const {
    switch (Scenario()) {
        case Game::MpScenario::TheLivingDaylights:
            return AliveWithFlag();
        case Game::MpScenario::TheManWithTheGoldenGun:
            return HasGoldenGun();
        default:
            return false;
    }
}

void Stage::TickScenarioItem() {
    PlayerStats& stats = CurStats();
    switch (Scenario()) {
        case Game::MpScenario::TheLivingDaylights:
            if (AliveWithFlag()) {
                if (Cur().weapons.Ready() && Cur().weapons.WeaponId(HandRight) != kItemToken) {
                    Cur().weapons.EquipWeapon(HandRight, kItemToken);
                    HandState& hand = Cur().weapons.MutableHand(HandRight);
                    if (hand.actionState == GunState::Fire) {
                        hand.actionState = GunState::SwitchLower;
                    }
                }
                stats.flagCounter += clock_.clockTimer;
                stats.haveTokenOrGoldenGun = 1;
            } else {
                stats.haveTokenOrGoldenGun = 0;
            }
            break;
        case Game::MpScenario::TheManWithTheGoldenGun:
            stats.haveTokenOrGoldenGun = HasGoldenGun() ? 1 : 0;
            break;
        default:
            break;
    }
}

void Stage::TickYolt() {
    if (Scenario() != Game::MpScenario::YouOnlyLiveTwice || clock_.clockTimer == 0) {
        return;
    }
    std::array<YoltPlayer, kMaxPlayers> players{};
    std::array<int, kMaxPlayers> orderOut{};
    for (int i = 0; i < playerCount_; i++) {
        const PlayerState& state = players_[static_cast<size_t>(i)];
        YoltPlayer& player = players[static_cast<size_t>(i)];
        player.alive = !state.health.IsDead();
        for (int killer = 0; killer < playerCount_; killer++) {
            player.killedCount += stats_[static_cast<size_t>(killer)].killCounts[static_cast<size_t>(i)];
        }
        player.fullyDead =
            state.health.RedBloodFinished() && state.health.DeathAnimFinished() && state.colour.IsComplete();
        orderOut[static_cast<size_t>(i)] = stats_[static_cast<size_t>(i)].orderOutInYolt;
    }
    yoltOutcome_ = TickYoltRound(playerCount_, players, orderOut);
    for (int i = 0; i < playerCount_; i++) {
        stats_[static_cast<size_t>(i)].orderOutInYolt = orderOut[static_cast<size_t>(i)];
    }
    // TODO(https://github.com/liz-baker/frigate/issues/388): the outcome
    // calls mpwatchSetStopPlayFlag (StopPlay) or mpCalculateAwards (End).
}

} // namespace Frigate::Level
