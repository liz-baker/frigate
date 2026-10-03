#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "game/save/SaveManager.h"

namespace Frigate::Game::Save {

// What the file select, mission select, difficulty, cheat and mission
// complete screens ask of the player's folders: file.c's current-folder
// wrappers and selected_folder_num / selected_difficulty, plus the file2.c
// progression queries and updates, over a SaveManager. Screens use this
// instead of reaching into SaveData. Folders are 0 to 3 (kFolder1...); level
// and difficulty arguments are the decomp's LEVEL_SOLO_SEQUENCE and DIFFICULTY
// values (SaveData.h). Updates are persisted as they are made.
class SaveProgress {
  public:
    explicit SaveProgress(std::shared_ptr<SaveManager> save);

    // selected_folder_num: the folder the player is working in. fileSetCurrentFolder
    // stores whatever it is given.
    int32_t CurrentFolder() const {
        return currentFolder_;
    }
    void SetCurrentFolder(int32_t folder) {
        currentFolder_ = folder;
    }
    // set_selected_foldernum_and_copy_demo_eeprom: keeps the current folder
    // aside and switches to the demo recordings' folder, loaded with `save`.
    void SelectDemoFolder(const SaveData& save);
    // selected_folder_num_copy, as left by SelectDemoFolder.
    int32_t PreviousFolder() const {
        return previousFolder_;
    }

    // selected_difficulty, kDifficultyMulti until a mission's difficulty is
    // chosen. set_selected_difficulty maps anything but Secret Agent, 00 Agent
    // and 007 to Agent.
    int32_t SelectedDifficulty() const {
        return selectedDifficulty_;
    }
    void SetSelectedDifficulty(int32_t difficulty);

    // sub_GAME_7F01D61C (fileCopySave of the current folder): a copy of its
    // save, blank when it has none.
    SaveData CopyCurrentFolderSave() const;

    // fileLoadSaveSettingsForSelectedFolder (fileLoadSettingsForFolder of the
    // current folder): hands the folder's save to `apply`, which sets the
    // volumes and options from it (GameSettings.h). Does nothing when the
    // folder has no save.
    void LoadSaveSettingsForSelectedFolder(const std::function<void(const SaveData&)>& apply) const;

    // fileGetBondForCurrentFolder / fileUpdateBondInCurrentFolder.
    int32_t BondForCurrentFolder() const;
    void UpdateBondInCurrentFolder();

    // deleteCurrentSelectedFolder (fileClearSavefileForFolder): saves the
    // current settings into the current folder if they changed. The capture
    // is fileSaveSettingsForFolder (GameSettings.h); without one this leaves
    // the folder as it is.
    void SetSettingsCapture(SaveManager::SettingsCapture capture) {
        settingsCapture_ = std::move(capture);
    }
    void DeleteCurrentSelectedFolder();

    // fileIsStageUnlockedAtDifficulty: Locked, Unlocked or Completed.
    StageStatus StageStatusFor(int32_t folder, int32_t level, int32_t difficulty) const;
    bool IsStageUnlocked(int32_t folder, int32_t level, int32_t difficulty) const {
        return StageStatusFor(folder, level, difficulty) != StageStatus::Locked;
    }
    bool IsStageCompleted(int32_t folder, int32_t level, int32_t difficulty) const {
        return StageStatusFor(folder, level, difficulty) == StageStatus::Completed;
    }
    // The folder's best time on a level in seconds, 0 when not completed. 007
    // mode keeps none: it reads 0x3FF once 007 is unlocked and 0 before.
    uint32_t BestTime(int32_t folder, int32_t level, int32_t difficulty) const;
    // fileUnlockStageInFolderAtDifficulty: records a completion time (seconds;
    // kept only when it beats the stored one) and the lower difficulties'.
    void UnlockStage(int32_t folder, int32_t level, int32_t difficulty, int32_t seconds);

    // fileGetHighestStageDifficultyCompletedForFolder.
    StageDifficulty HighestStageDifficultyCompleted(int32_t folder) const;
    // fileGetHighestStageUnlockedForFolder / fileGetHighestStageUnlockedAnyFolder.
    int32_t HighestStageUnlocked(int32_t folder) const;
    int32_t HighestStageUnlockedAnyFolder() const;

    // fileIs007ModeUnlocked.
    bool Is007ModeUnlocked(int32_t folder) const;
    // The Cradle, Aztec and Egypt completions that unlock the bonus levels
    // and screens, for one folder or any.
    bool IsCradleCompleted(int32_t folder) const;
    bool IsAztecCompletedOnSecretOr00(int32_t folder) const;
    bool IsEgyptCompletedOn00(int32_t folder) const;
    bool IsCradleCompletedAnyFolder() const;
    bool IsAztecCompletedOnSecretOr00AnyFolder() const;
    bool IsEgyptCompletedOn00AnyFolder() const;

    // fileGetIsCheatUnlocked over a folder's save, and fileSaveFolderUnlockCheat.
    // Cheats are indexed as the decomp's save bits are (CHEAT_UNLOCK_x -
    // CHEAT_UNLOCK_PAINTBALL, or a level number for the level-reward cheats).
    bool IsCheatUnlocked(int32_t folder, int32_t cheat) const;
    void UnlockCheat(int32_t folder, int32_t cheat);

    // fileDeleteSaveForFolder, fileCopyFolderToFirstFree, fileInitializeAllTimes.
    void DeleteFolder(int32_t folder);
    void CopyFolderToFirstFree(int32_t folder);
    void InitializeAllTimes(int32_t folder);

    SaveManager& Manager() {
        return *save_;
    }

  private:
    std::shared_ptr<SaveManager> save_;
    int32_t currentFolder_ = kFolder1;
    int32_t previousFolder_ = kFolder1;
    int32_t selectedDifficulty_ = kDifficultyMulti;
    SaveManager::SettingsCapture settingsCapture_;
};

} // namespace Frigate::Game::Save
