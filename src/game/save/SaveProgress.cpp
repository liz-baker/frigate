#include "game/save/SaveProgress.h"

#include <utility>

namespace Frigate::Game::Save {

SaveProgress::SaveProgress(std::shared_ptr<SaveManager> save) : save_(std::move(save)) {
}

void SaveProgress::SelectDemoFolder(const SaveData& save) {
    previousFolder_ = currentFolder_;
    currentFolder_ = kRamRomFolderNum;
    save_->CopyDemoSaveToRamRomSave(static_cast<uint32_t>(kRamRomFolderNum), save);
}

void SaveProgress::SetSelectedDifficulty(int32_t difficulty) {
    switch (difficulty) {
        case kDifficultySecret:
        case kDifficulty00:
        case kDifficulty007:
            selectedDifficulty_ = difficulty;
            break;
        default:
            selectedDifficulty_ = kDifficultyAgent;
            break;
    }
}

SaveData SaveProgress::CopyCurrentFolderSave() const {
    return save_->CopySave(currentFolder_);
}

int32_t SaveProgress::BondForCurrentFolder() const {
    return save_->GetBondForFolder(static_cast<uint32_t>(currentFolder_));
}

void SaveProgress::UpdateBondInCurrentFolder() {
    save_->UpdateSelectedBondInSave(currentFolder_);
}

void SaveProgress::LoadSaveSettingsForSelectedFolder(const std::function<void(const SaveData&)>& apply) const {
    if (currentFolder_ < 0) {
        return;
    }
    if (const SaveData* save = save_->GetSaveForFoldernum(static_cast<uint32_t>(currentFolder_))) {
        apply(*save);
    }
}

void SaveProgress::DeleteCurrentSelectedFolder() {
    save_->ClearSavefileForFolder(currentFolder_, settingsCapture_);
}

StageStatus SaveProgress::StageStatusFor(int32_t folder, int32_t level, int32_t difficulty) const {
    return save_->IsStageUnlockedAtDifficulty(folder, level, difficulty);
}

uint32_t SaveProgress::BestTime(int32_t folder, int32_t level, int32_t difficulty) const {
    if (folder < 0) {
        return 0;
    }
    const SaveData* save = save_->GetSaveForFoldernum(static_cast<uint32_t>(folder));
    return save ? save_->GetStageDifficultyTime(*save, level, difficulty) : 0;
}

void SaveProgress::UnlockStage(int32_t folder, int32_t level, int32_t difficulty, int32_t seconds) {
    save_->UnlockStageInFolderAtDifficulty(folder, level, difficulty, seconds);
}

StageDifficulty SaveProgress::HighestStageDifficultyCompleted(int32_t folder) const {
    return save_->GetHighestStageDifficultyCompletedForFolder(folder);
}

int32_t SaveProgress::HighestStageUnlocked(int32_t folder) const {
    return save_->GetHighestStageUnlockedForFolder(folder);
}

int32_t SaveProgress::HighestStageUnlockedAnyFolder() const {
    return save_->GetHighestStageUnlockedAnyFolder();
}

bool SaveProgress::Is007ModeUnlocked(int32_t folder) const {
    return folder >= 0 && save_->Is007ModeUnlocked(static_cast<uint32_t>(folder));
}

bool SaveProgress::IsCradleCompleted(int32_t folder) const {
    return save_->IsCradleCompletedForFolder(folder);
}

bool SaveProgress::IsAztecCompletedOnSecretOr00(int32_t folder) const {
    return save_->IsAztecCompletedOnSecretOr00ForFolder(folder);
}

bool SaveProgress::IsEgyptCompletedOn00(int32_t folder) const {
    return save_->IsEgyptCompletedOn00ForFolder(folder);
}

bool SaveProgress::IsCradleCompletedAnyFolder() const {
    return save_->IsCradleCompletedAnyFolder();
}

bool SaveProgress::IsAztecCompletedOnSecretOr00AnyFolder() const {
    return save_->IsAztecCompletedOnSecretOr00AnyFolder();
}

bool SaveProgress::IsEgyptCompletedOn00AnyFolder() const {
    return save_->IsEgyptCompletedOn00AnyFolder();
}

bool SaveProgress::IsCheatUnlocked(int32_t folder, int32_t cheat) const {
    return save_->IsCheatUnlockedForFolder(folder, cheat);
}

void SaveProgress::UnlockCheat(int32_t folder, int32_t cheat) {
    save_->SaveFolderUnlockCheat(folder, cheat);
}

void SaveProgress::DeleteFolder(int32_t folder) {
    save_->DeleteSaveForFolder(folder);
}

void SaveProgress::CopyFolderToFirstFree(int32_t folder) {
    save_->CopyFolderToFirstFree(folder);
}

void SaveProgress::InitializeAllTimes(int32_t folder) {
    if (folder >= 0) {
        save_->InitializeAllTimes(static_cast<uint32_t>(folder));
    }
}

} // namespace Frigate::Game::Save
