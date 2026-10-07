#include "game/MultiplayerUnlocks.h"

#include "game/save/SaveProgress.h"

namespace Frigate::Game {

namespace {

// LEVEL_SOLO_SEQUENCE values the stage rules name.
constexpr int kLevelBunker2 = 8;
constexpr int kLevelArchives = 10;
constexpr int kLevelCaverns = 16;

// multi_stage_setups' unlock_after, in MpStage order.
constexpr std::array<int, kMpStageCount> kUnlockAfter{
    -1, -1, -1, -1, -1, -1, -1, Save::kLevelFacility, kLevelBunker2, kLevelArchives, kLevelCaverns, Save::kLevelEgypt,
};

} // namespace

int SelectedPlayerCount(bool multiplayerMode, int selectedNumPlayers) {
    return multiplayerMode ? selectedNumPlayers : 1;
}

bool PlayersHaveSelectedChar(int character, int playerCount, const std::array<int, kMpMaxPlayers>& playerChar,
                             const std::array<bool, kMpMaxPlayers>& hasSelected) {
    bool selected = false;
    for (int i = 0; i < playerCount && i < kMpMaxPlayers; i++) {
        if (hasSelected[i] && character == playerChar[i]) {
            selected = true;
        }
    }
    return selected;
}

int MultiplayerUnlocks::UnlockAfter(int stage) {
    return stage < 0 || stage >= kMpStageCount ? -1 : kUnlockAfter[static_cast<size_t>(stage)];
}

bool MultiplayerUnlocks::IsStageUnlocked(const Save::SaveProgress& progress, int stage, int playerCount) {
    if (stage < 0 || stage >= kMpStageCount ||
        MultiplayerSettings::StagePlayers(static_cast<MpStage>(stage)).max < playerCount) {
        return false;
    }
    const int unlockAfter = UnlockAfter(stage);
    return unlockAfter == -1 || progress.IsStageUnlocked(progress.CurrentFolder(), unlockAfter, Save::kDifficultyAgent);
}

int MultiplayerUnlocks::StageWhenOpeningOptions(const Save::SaveProgress& progress, int selectedStage,
                                                int playerCount) {
    return IsStageUnlocked(progress, selectedStage, playerCount) ? selectedStage : kMpStageTemple;
}

int MultiplayerUnlocks::StageForPlayerCount(int selectedStage, int playerCount) {
    if (selectedStage < 0 || selectedStage >= kMpStageCount) {
        return kMpStageTemple;
    }
    return MultiplayerSettings::StagePlayers(static_cast<MpStage>(selectedStage)).max < playerCount ? kMpStageTemple
                                                                                                    : selectedStage;
}

int MultiplayerUnlocks::HighlightFallback(const Save::SaveProgress& progress, int highlighted, int playerCount) {
    while (highlighted > kMpStageRandom && !IsStageUnlocked(progress, highlighted, playerCount)) {
        highlighted--;
    }
    return highlighted;
}

int MultiplayerUnlocks::ResolveRandomStage(const Save::SaveProgress& progress, int playerCount,
                                           const std::function<uint32_t()>& nextRandom) {
    int stage;
    do {
        stage = 1 + static_cast<int>(nextRandom() % (kMpStageCount - 1));
    } while (!IsStageUnlocked(progress, stage, playerCount));
    return stage;
}

std::array<bool, kMpStageCount> MultiplayerUnlocks::UnlockedStages(const Save::SaveProgress& progress,
                                                                   int playerCount) {
    std::array<bool, kMpStageCount> unlocked{};
    for (int stage = 0; stage < kMpStageCount; stage++) {
        unlocked[stage] = IsStageUnlocked(progress, stage, playerCount);
    }
    return unlocked;
}

void MultiplayerUnlocks::OnCharacterSelectEntered(const Save::SaveProgress& progress, int playerCount,
                                                  std::array<int, kMpMaxPlayers>& playerChar,
                                                  const std::array<bool, kMpMaxPlayers>& hasSelected) {
    if (numChars_ == kMpCharsAll) {
        return;
    }
    if (progress.IsStageCompleted(progress.CurrentFolder(), Save::kLevelCradle, Save::kDifficultyAgent)) {
        numChars_ = kMpCharsAfterCradle;
        return;
    }
    numChars_ = kMpCharsDefault;
    for (int& character : playerChar) {
        if (character < numChars_) {
            continue;
        }
        int row = 0;
        while (row < numChars_ && PlayersHaveSelectedChar(row, playerCount, playerChar, hasSelected)) {
            row++;
        }
        character = row;
    }
}

} // namespace Frigate::Game
