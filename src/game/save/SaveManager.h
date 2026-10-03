#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "game/save/SaveData.h"
#include "game/save/SaveStorage.h"

namespace Frigate::Game::Save {

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
// Not ported yet: the progression queries and best-time updates built on
// these (stage unlocks, highest stage, fileUnlockStageInFolderAtDifficulty,
// deleting and copying a folder) are
// TODO(https://github.com/liz-baker/frigate/issues/290). The folder-clear and
// settings routines (fileClearSavefileForFolder, fileSaveSettingsForFolder
// and fileLoadSettingsForFolder's music and sound volumes, which need the
// audio and options state), and file.c's current-folder wrappers over
// selected_folder_num, belong to the file select screen,
// TODO(https://github.com/liz-baker/frigate/issues/95). The 007-mode
// sliders in file.c are TODO(https://github.com/liz-baker/frigate/issues/293)
// and end_of_mission_briefing is
// TODO(https://github.com/liz-baker/frigate/issues/296).
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
};

} // namespace Frigate::Game::Save
