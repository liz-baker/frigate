#include "game/DevCheats.h"

#include <mutex>
#include <utility>
#include <vector>

#include "level/CheatMasks.h"
#include "level/Stage.h"

namespace Frigate::Game::DevCheats {

namespace {

namespace Cheat = Menu::Cheat;

std::mutex gMutex;
Snapshot gSnapshot;
std::vector<std::pair<int32_t, bool>> gRequests;

} // namespace

Snapshot Current() {
    const std::lock_guard<std::mutex> lock(gMutex);
    return gSnapshot;
}

void Request(int32_t cheat, bool on) {
    const std::lock_guard<std::mutex> lock(gMutex);
    gRequests.emplace_back(cheat, on);
}

void Sync(Level::Stage* stage) {
    std::vector<std::pair<int32_t, bool>> requests;
    {
        const std::lock_guard<std::mutex> lock(gMutex);
        requests.swap(gRequests);
    }
    Snapshot snapshot;
    if (stage != nullptr) {
        for (const auto& [cheat, on] : requests) {
            if (AppliesInStage(cheat, stage->PlayerCount())) {
                stage->SetCheatFromDevTools(cheat, on);
            }
        }
        snapshot.live = true;
        snapshot.playerCount = stage->PlayerCount();
        for (int32_t cheat = 1; cheat < Cheat::kInvalid; cheat++) {
            snapshot.active[static_cast<size_t>(cheat)] = stage->FirstPlayerCheatActive(cheat);
        }
    }
    const std::lock_guard<std::mutex> lock(gMutex);
    gSnapshot = snapshot;
}

bool AppliesInStage(int32_t cheat, int playerCount) {
    if (cheat == Cheat::kEnemyRockets) {
        return true;
    }
    if (cheat < Cheat::kInvincibility || cheat > Cheat::k2xLaser) {
        return false;
    }
    return (Level::CheatMaskField(cheat) & Level::CheatApplicableMask(playerCount)) != 0;
}

} // namespace Frigate::Game::DevCheats
