#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/save/SaveManager.h"

namespace Frigate::Game::Save {

// The save editor behind the menu's Dev Tools tab: edits a copy of one
// folder's save and writes it back only on Apply, through
// SaveManager::OverwriteSaveSlotWithNewSave (the route every game-side change
// takes: wear levelling, checksums). Edits keep to states the game itself can
// leave behind: a completion on a difficulty implies one on every easier
// difficulty, and times stay in the stored 10-bit range. No ImGui here.
class SaveEditor {
  public:
    // The shortest and longest stored time, in seconds. A stage with no time
    // is not completed; 0x3FF is what the game stores for the easier
    // difficulties it fills in.
    static constexpr int32_t kMinTime = 1;
    static constexpr int32_t kMaxTime = 0x3FF;

    explicit SaveEditor(std::shared_ptr<SaveManager> manager);

    // Loads the folder's save into the working copy, dropping any edits.
    void SelectFolder(int32_t folder);
    int32_t Folder() const {
        return folder_;
    }
    // Reloads the working copy from the manager, dropping any edits.
    void Revert();

    const SaveData& Working() const {
        return working_;
    }
    const SaveData& Original() const {
        return original_;
    }
    bool IsDirty() const {
        return working_ != original_;
    }
    // One line per difference between the original and the working copy.
    std::vector<std::string> Changes() const;

    // Level-reward cheats: save bit n is the cheat the n-th level awards.
    // Magnum, Laser and Golden Gun have no bit; the game derives them from
    // completions (CradleCompleted and the two after it).
    bool IsCheatUnlocked(int32_t level) const;
    void SetCheatUnlocked(int32_t level, bool unlocked);

    // Difficulties Agent, Secret Agent and 00 Agent only; 007 mode stores
    // no completions of its own (see Flag007 and Is007ModeUnlocked).
    bool IsStageCompleted(int32_t level, int32_t difficulty) const;
    // Seconds, 0 when not completed.
    int32_t StageTime(int32_t level, int32_t difficulty) const;
    // Completing a stage records kMaxTime where it has no time and completes
    // the easier difficulties the same way; un-completing it clears the
    // harder ones too.
    void SetStageCompleted(int32_t level, int32_t difficulty, bool completed);
    // Clamped to [kMinTime, kMaxTime]; completes the stage as above.
    void SetStageTime(int32_t level, int32_t difficulty, int32_t seconds);
    // Every level on one difficulty, with the easier ones.
    void CompleteAll(int32_t difficulty);
    void ClearAll(int32_t difficulty);

    // The 007 flag byte's bit 0. 007 mode is also open once every level has
    // a time on 00 Agent, whatever the flag says.
    bool Flag007() const;
    void SetFlag007(bool set);
    bool Is007ModeUnlocked() const;

    // The completions that expand the intro cast and award the bonus guns.
    bool CradleCompleted() const;
    bool AztecCompletedOnSecretOr00() const;
    bool EgyptCompletedOn00() const;

    // Writes the working copy through the manager. False, with LastError set,
    // when the storage is missing or no slot is free; the working copy is
    // kept then. On success the working copy is reloaded from the manager.
    bool Apply();
    const std::string& LastError() const {
        return lastError_;
    }

    SaveManager& Manager() {
        return *manager_;
    }

  private:
    void Load();

    std::shared_ptr<SaveManager> manager_;
    int32_t folder_ = kFolder1;
    SaveData original_;
    SaveData working_;
    std::string lastError_;
};

// The level a LEVEL_SOLO_SEQUENCE number is, as the mission select names it.
const char* LevelName(int32_t level);
// Display names of the level-reward cheats, by the level that awards them.
const char* RewardCheatName(int32_t level);
// DIFFICULTY.
const char* DifficultyName(int32_t difficulty);

} // namespace Frigate::Game::Save
