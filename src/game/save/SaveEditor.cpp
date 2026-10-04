#include "game/save/SaveEditor.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "game/menu/CheatAvailability.h"
#include "game/menu/CheatIds.h"

namespace Frigate::Game::Save {

namespace {

constexpr const char* kLevelNames[kSoloLevelCount] = {
    "Dam",       "Facility", "Runway",  "Surface 1", "Bunker 1", "Silo",   "Frigate", "Surface 2", "Bunker 2", "Statue",
    "Archives",  "Streets",  "Depot",   "Train",     "Jungle",   "Control", "Caverns", "Cradle",   "Aztec",    "Egypt",
};

const char* CheatNameFor(int32_t cheat) {
    using namespace Menu::Cheat;
    switch (cheat) {
        case kInvincibility:
            return "Invincibility";
        case kAllGuns:
            return "All Guns";
        case kNoRadarMp:
            return "No Radar (multi)";
        case kFastAnimation:
            return "Fast Animation";
        case kInvisibility:
            return "Invisibility";
        case kInfiniteAmmo:
            return "Infinite Ammo";
        case kDkMode:
            return "DK Mode";
        case kTinyBond:
            return "Tiny Bond";
        case kSlowAnimation:
            return "Slow Animation";
        case kPaintball:
            return "Paintball Mode";
        case kSilverPp7:
            return "Silver PP7";
        case kGoldPp7:
            return "Gold PP7";
        case kEnemyRockets:
            return "Enemy Rockets";
        case k2xRocketLauncher:
            return "2x Rocket Launcher";
        case k2xGrenadeLauncher:
            return "2x Grenade Launcher";
        case k2xRcp90:
            return "2x RC-P90";
        case k2xThrowingKnife:
            return "2x Throwing Knife";
        case k2xHuntingKnife:
            return "2x Hunting Knife";
        case k2xLaser:
            return "2x Laser";
        case kTurboMode:
            return "Turbo Mode";
        default:
            return "?";
    }
}

bool StageInRange(int32_t level, int32_t difficulty) {
    return level >= kLevelDam && level < kSoloLevelCount && difficulty >= kDifficultyAgent &&
           difficulty <= kDifficulty00;
}

std::string Describe(int32_t level, int32_t difficulty, int32_t from, int32_t to) {
    char buffer[96];
    if (from == 0) {
        std::snprintf(buffer, sizeof buffer, "%s, %s: completed in %d:%02d", LevelName(level),
                      DifficultyName(difficulty), to / 60, to % 60);
    } else if (to == 0) {
        std::snprintf(buffer, sizeof buffer, "%s, %s: no longer completed", LevelName(level),
                      DifficultyName(difficulty));
    } else {
        std::snprintf(buffer, sizeof buffer, "%s, %s: time %d:%02d to %d:%02d", LevelName(level),
                      DifficultyName(difficulty), from / 60, from % 60, to / 60, to % 60);
    }
    return buffer;
}

} // namespace

SaveEditor::SaveEditor(std::shared_ptr<SaveManager> manager) : manager_(std::move(manager)) {
    Load();
}

void SaveEditor::Load() {
    SaveData save = manager_->CopySave(folder_);
    // A folder without a live save starts out as the blank the game would
    // build for it.
    if (manager_->GetSaveForFoldernum(static_cast<uint32_t>(folder_)) == nullptr) {
        SetSaveFolder(save, static_cast<uint32_t>(folder_));
    }
    original_ = save;
    working_ = save;
}

void SaveEditor::SelectFolder(int32_t folder) {
    if (folder < kFolder1 || folder >= kFolderCount) {
        return;
    }
    folder_ = folder;
    lastError_.clear();
    Load();
}

void SaveEditor::Revert() {
    Load();
}

std::vector<std::string> SaveEditor::Changes() const {
    std::vector<std::string> changes;
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        const bool before = Save::IsCheatUnlocked(original_, level);
        const bool after = Save::IsCheatUnlocked(working_, level);
        if (before != after) {
            changes.push_back(std::string(RewardCheatName(level)) + (after ? ": unlocked" : ": locked"));
        }
    }
    for (int32_t difficulty = kDifficultyAgent; difficulty <= kDifficulty00; difficulty++) {
        for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
            const auto before = static_cast<int32_t>(GetPackedStageTime(original_, level, difficulty));
            const auto after = static_cast<int32_t>(GetPackedStageTime(working_, level, difficulty));
            if (before != after) {
                changes.push_back(Describe(level, difficulty, before, after));
            }
        }
    }
    if ((original_.flag007 & 1) != (working_.flag007 & 1)) {
        changes.push_back((working_.flag007 & 1) ? "007 mode flag: set" : "007 mode flag: cleared");
    }
    return changes;
}

bool SaveEditor::IsCheatUnlocked(int32_t level) const {
    return Save::IsCheatUnlocked(working_, level);
}

void SaveEditor::SetCheatUnlocked(int32_t level, bool unlocked) {
    if (unlocked) {
        Save::SetCheatUnlocked(working_, level);
    } else {
        ClearCheatUnlocked(working_, level);
    }
}

bool SaveEditor::IsStageCompleted(int32_t level, int32_t difficulty) const {
    return StageTime(level, difficulty) != 0;
}

int32_t SaveEditor::StageTime(int32_t level, int32_t difficulty) const {
    return StageInRange(level, difficulty) ? static_cast<int32_t>(GetPackedStageTime(working_, level, difficulty)) : 0;
}

void SaveEditor::SetStageCompleted(int32_t level, int32_t difficulty, bool completed) {
    if (!StageInRange(level, difficulty)) {
        return;
    }
    if (completed) {
        for (int32_t i = difficulty; i >= kDifficultyAgent; i--) {
            if (StageTime(level, i) == 0) {
                SetPackedStageTime(working_, level, i, kMaxTime);
            }
        }
    } else {
        for (int32_t i = difficulty; i <= kDifficulty00; i++) {
            ClearPackedStageTime(working_, level, i);
        }
    }
}

void SaveEditor::SetStageTime(int32_t level, int32_t difficulty, int32_t seconds) {
    if (!StageInRange(level, difficulty)) {
        return;
    }
    SetStageCompleted(level, difficulty, true);
    SetPackedStageTime(working_, level, difficulty, std::clamp(seconds, kMinTime, kMaxTime));
}

void SaveEditor::CompleteAll(int32_t difficulty) {
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        SetStageCompleted(level, difficulty, true);
    }
}

void SaveEditor::ClearAll(int32_t difficulty) {
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        SetStageCompleted(level, difficulty, false);
    }
}

bool SaveEditor::Flag007() const {
    return (working_.flag007 & 1) != 0;
}

void SaveEditor::SetFlag007(bool set) {
    if (set) {
        working_.flag007 |= 1;
    } else {
        working_.flag007 &= static_cast<uint8_t>(~1);
    }
}

bool SaveEditor::Is007ModeUnlocked() const {
    return Save::Is007ModeUnlocked(working_);
}

bool SaveEditor::CradleCompleted() const {
    return IsStageCompleted(kLevelCradle, kDifficultyAgent) || IsStageCompleted(kLevelCradle, kDifficultySecret) ||
           IsStageCompleted(kLevelCradle, kDifficulty00);
}

bool SaveEditor::AztecCompletedOnSecretOr00() const {
    return IsStageCompleted(kLevelAztec, kDifficultySecret) || IsStageCompleted(kLevelAztec, kDifficulty00);
}

bool SaveEditor::EgyptCompletedOn00() const {
    return IsStageCompleted(kLevelEgypt, kDifficulty00);
}

bool SaveEditor::Apply() {
    lastError_.clear();
    if (!manager_->Storage().Present()) {
        lastError_ = "No save storage is available.";
        return false;
    }
    if (manager_->GetSaveFlagDoResetAnyFolder() < 0) {
        lastError_ = "No free save slot to write to.";
        return false;
    }
    SaveData* live = manager_->GetSaveForFoldernum(static_cast<uint32_t>(folder_));
    manager_->OverwriteSaveSlotWithNewSave(live, working_);
    Load();
    return true;
}

const char* LevelName(int32_t level) {
    return level >= kLevelDam && level < kSoloLevelCount ? kLevelNames[level] : "?";
}

const char* RewardCheatName(int32_t level) {
    for (int32_t cheat = Menu::Cheat::kExtraMpChars; cheat < Menu::Cheat::kInvalid; cheat++) {
        if (Menu::RewardLevel(cheat) == level) {
            return CheatNameFor(cheat);
        }
    }
    return "?";
}

const char* DifficultyName(int32_t difficulty) {
    switch (difficulty) {
        case kDifficultyAgent:
            return "Agent";
        case kDifficultySecret:
            return "Secret Agent";
        case kDifficulty00:
            return "00 Agent";
        case kDifficulty007:
            return "007";
        default:
            return "?";
    }
}

} // namespace Frigate::Game::Save
