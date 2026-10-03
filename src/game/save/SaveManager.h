#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include "game/save/SaveData.h"
#include "game/save/SaveStorage.h"

namespace Frigate::Game::Save {

// STAGESTATUS.
enum class StageStatus : int32_t {
    Locked = 0,
    Unlocked = 1,
    NotCompleted = 2,
    Completed = 3,
};

// A level and difficulty pair, as fileGetHighestStageDifficultyCompletedForFolder
// reports them: level -1 and kDifficultyMulti when nothing is completed.
struct StageDifficulty {
    int32_t level;
    int32_t difficulty;
};

// SAVESLOT1..SAVESLOT4 are the four folders' slots plus one spare that wear
// levelling rotates through; SAVESLOTRAMROM, the demo recordings' folder, is
// memory only.
constexpr size_t kEepromSaveSlots = 5;
constexpr size_t kSaveSlotRamRom = 5;
constexpr size_t kSaveSlotCount = 6;

// The decomp's `saves[]` and `save_selected_bond[]` globals with the file2.c
// routines that read and write them, over an injected EEPROM. Slots are
// referenced by pointer as in the decomp; pointers from GetSaveForFoldernum
// stay valid for the manager's lifetime but a slot's contents (and which slot
// holds a folder) change when a save is written.
//
// fileSaveSettingsForFolder and fileLoadSettingsForFolder are in
// GameSettings.h. The 007-mode
// sliders in file.c are TODO(https://github.com/liz-baker/frigate/issues/293)
// and end_of_mission_briefing is
// TODO(https://github.com/liz-baker/frigate/issues/296). The current-folder
// wrappers in file.c are SaveProgress.h.
class SaveManager {
  public:
    explicit SaveManager(std::shared_ptr<SaveStorage> storage);

    // fileValidateSaves: reads the EEPROM, rewrites a bad header, resets every
    // slot whose checksum fails, then makes sure each folder has exactly one
    // live slot (creating a blank one where it has none). Missing or short
    // data reads as an all-zero chip, which fails every check and so ends up
    // as four blank folders.
    void ValidateSaves();

    static bool IsFolderValid(int32_t folder);

    // fileGetSaveForFoldernum: the live slot of a folder, or null. The demo
    // recordings' folder always has one.
    SaveData* GetSaveForFoldernum(uint32_t folder);
    const SaveData* GetSaveForFoldernum(uint32_t folder) const;
    // fileCopySave: a copy of the folder's save, blank when it has none.
    SaveData CopySave(int32_t folder) const;

    // fileGetSaveFlagDoReset_any_folder: the first free slot, or -1.
    int32_t GetSaveFlagDoResetAnyFolder() const;

    // fileWriteSave: stamps the checksums and writes the slot to the EEPROM.
    // Slots outside the EEPROM are left alone.
    void WriteSave(SaveData& save);
    // fileResetSave: blanks the slot and writes it.
    void ResetSave(SaveData& save);
    // fileBuildWriteNewSave: turns a free slot into an empty save for `folder`.
    void BuildWriteNewSave(uint32_t folder);
    // fileOverwriteSaveSlotWithNewSave: stores `save2` in a free slot with the
    // wear-levelling counter one past `save1`'s and then frees `save1` (null
    // when the folder had no save). Does nothing when no slot is free. This is
    // how every change to a folder reaches the EEPROM.
    void OverwriteSaveSlotWithNewSave(SaveData* save1, const SaveData& save2);

    // fileResetRamRomSave / fileCopyDemoSaveToRamRomSave.
    void ResetRamRomSave();
    void CopyDemoSaveToRamRomSave(uint32_t folder, const SaveData& save);

    // fileGetSaveStageDifficultyTime: the packed best time, or for 007 mode
    // 0x3FF once the folder has 007 unlocked and 0 before.
    uint32_t GetStageDifficultyTime(const SaveData& save, int32_t level, int32_t difficulty) const;
    // fileGetSaveStageCompletedForDifficulty.
    bool GetStageCompletedForDifficulty(const SaveData& save, int32_t level, int32_t difficulty) const;
    // fileCheckSaveStageDifficultyTime: stores `newtime` when the level has no
    // time on that difficulty yet or the new time is lower. Edits `save` in
    // place; persisting it is OverwriteSaveSlotWithNewSave's job.
    void CheckStageDifficultyTime(SaveData& save, int32_t level, int32_t difficulty, int32_t newtime) const;
    // fileIs007ModeUnlocked.
    bool Is007ModeUnlocked(uint32_t folder) const;

    // fileSaveFolderUnlockCheat: unlocks a cheat in the folder and persists it.
    void SaveFolderUnlockCheat(int32_t folder, int32_t cheat);
    // fileGetIsCheatUnlocked over the folder's live save; false without one.
    bool IsCheatUnlockedForFolder(int32_t folder, int32_t cheat) const;

    // fileIsStageUnlockedAtDifficulty.
    StageStatus IsStageUnlockedAtDifficulty(int32_t folder, int32_t level, int32_t difficulty) const;
    // The debug menu's get_debug_enable_agent_levels_flag and
    // get_debug_enable_all_levels_flag, which fileIsStageUnlockedAtDifficulty
    // consults for a folder with no save. Both start off.
    void SetDebugEnableAgentLevels(bool enable) {
        debugEnableAgentLevels_ = enable;
    }
    void SetDebugEnableAllLevels(bool enable) {
        debugEnableAllLevels_ = enable;
    }
    // fileUnlockStageInFolderAtDifficulty: records `newtime` for the level
    // (and a time on every easier difficulty that has none) and persists it.
    void UnlockStageInFolderAtDifficulty(int32_t folder, int32_t level, int32_t difficulty, int32_t newtime);
    // fileGetHighestStageDifficultyCompletedForFolder.
    StageDifficulty GetHighestStageDifficultyCompletedForFolder(int32_t folder) const;
    // fileGetHighestStageUnlockedForFolder / ...AnyFolder.
    int32_t GetHighestStageUnlockedForFolder(int32_t folder) const;
    int32_t GetHighestStageUnlockedAnyFolder() const;
    // fileIsCradleCompletedForFolder, fileIsAztecCompletedOnSecretOr00ForFolder,
    // fileIsEgyptCompletedOn00ForFolder and their any-folder forms
    // (fileIsCradleCompletedAnyFolder, check_aztec_completed_any_folder_secret_00,
    // fileIsEgyptCompletedOn00AnyFolder).
    bool IsCradleCompletedForFolder(int32_t folder) const;
    bool IsAztecCompletedOnSecretOr00ForFolder(int32_t folder) const;
    bool IsEgyptCompletedOn00ForFolder(int32_t folder) const;
    bool IsCradleCompletedAnyFolder() const;
    bool IsAztecCompletedOnSecretOr00AnyFolder() const;
    bool IsEgyptCompletedOn00AnyFolder() const;

    // fileDeleteSaveForFolder: blanks a folder that has anything completed.
    void DeleteSaveForFolder(int32_t folder);
    // fileInitializeAllTimes: gives every level on Agent, Secret Agent and 00
    // Agent a (maximum) time where it has none. Edits the live slot without
    // writing it, as the decomp does.
    void InitializeAllTimes(uint32_t folder);
    // fileCopyFolderToFirstFree: copies a folder that has something completed
    // over the first folder that is missing or empty.
    void CopyFolderToFirstFree(int32_t folder);
    // fileUpdateSelectedBondInSave: stores SelectedBond(folder) in the save.
    void UpdateSelectedBondInSave(int32_t folder);
    // fileClearSavefileForFolder: rewrites the folder's save with the current
    // settings if they differ. `captureSettings` is fileSaveSettingsForFolder,
    // which needs the audio and options state and so is supplied by the
    // settings code (GameSettings.h); without one the settings are left as
    // stored.
    using SettingsCapture = std::function<void(SaveData&)>;
    void ClearSavefileForFolder(int32_t folder, const SettingsCapture& captureSettings);

    // fileGetBondForFolder / fileSetSelectedBondTofolder. Retail GoldenEye
    // only has Brosnan; the decomp's ALL_BONDS build option is not enabled.
    uint8_t GetBondForFolder(uint32_t folder) const;
    void SetSelectedBondToFolder(int32_t folder, int32_t bond);
    // save_selected_bond[folder], as loaded from the EEPROM by ValidateSaves.
    int32_t SelectedBond(int32_t folder) const {
        return selectedBond_[static_cast<size_t>(folder)];
    }

    SaveStorage& Storage() {
        return *storage_;
    }
    const std::array<SaveData, kSaveSlotCount>& Slots() const {
        return saves_;
    }

  private:
    // The slot's index in saves_, or -1 for a pointer elsewhere.
    int32_t SlotIndex(const SaveData* save) const;
    // fileWriteSmallSave.
    void WriteSmallSave(const std::array<uint8_t, 32>& header);

    std::shared_ptr<SaveStorage> storage_;
    std::array<SaveData, kSaveSlotCount> saves_{};
    std::array<int32_t, kFolderCount> selectedBond_{};
    bool debugEnableAgentLevels_ = false;
    bool debugEnableAllLevels_ = false;
};

} // namespace Frigate::Game::Save
