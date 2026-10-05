#include "game/save/SaveEditor.h"

#include <cstdio>
#include <memory>
#include <string>

#include "game/menu/CheatAvailability.h"

using namespace Frigate::Game::Save;

namespace {

constexpr int32_t kLevelStatue = 9;

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

struct Rig {
    std::shared_ptr<MemorySaveStorage> storage = std::make_shared<MemorySaveStorage>();
    std::shared_ptr<SaveManager> manager;

    Rig() {
        Start();
    }

    // A new run of the game over the same EEPROM.
    void Start() {
        manager = std::make_shared<SaveManager>(storage);
        manager->ValidateSaves();
    }
};

int LiveSlots(const SaveManager& manager) {
    int live = 0;
    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        live += GetSaveFlagDoReset(manager.Slots()[i]) ? 0 : 1;
    }
    return live;
}

void TestEditsStayInTheCopyUntilApplied() {
    Rig rig;
    const auto imageBefore = rig.storage->Image();
    SaveEditor editor(rig.manager);

    Check(!editor.IsDirty() && editor.Changes().empty(), "a fresh editor has no changes");
    editor.SetCheatUnlocked(kLevelFacility, true);
    editor.SetStageTime(kLevelDam, kDifficultySecret, 95);
    editor.SetFlag007(true);

    Check(editor.IsDirty(), "edits make it dirty");
    Check(editor.Changes().size() == 4, "a cheat, two difficulties' times and the flag are listed");
    Check(!IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(0), kLevelFacility), "the live save is untouched");
    Check(rig.storage->Image() == imageBefore, "so is the EEPROM");

    editor.Revert();
    Check(!editor.IsDirty(), "revert drops the edits");
    Check(!editor.IsCheatUnlocked(kLevelFacility), "and the cheat edit");
}

void TestApplyWritesThroughTheManagerAndSurvivesARestart() {
    Rig rig;
    SaveEditor editor(rig.manager);
    editor.SelectFolder(2);
    editor.SetCheatUnlocked(kLevelEgypt, true);
    editor.SetStageTime(kLevelStatue, kDifficulty00, 301);
    editor.SetFlag007(true);

    Check(editor.Apply(), "apply succeeds");
    Check(!editor.IsDirty(), "the working copy matches what was written");
    Check(LiveSlots(*rig.manager) == 4, "still one live slot per folder");

    rig.Start();
    SaveEditor reloaded(rig.manager);
    reloaded.SelectFolder(2);
    Check(reloaded.IsCheatUnlocked(kLevelEgypt), "the cheat survives a restart");
    Check(reloaded.StageTime(kLevelStatue, kDifficulty00) == 301, "so does the time");
    Check(reloaded.StageTime(kLevelStatue, kDifficultySecret) == SaveEditor::kMaxTime,
          "with the easier difficulties filled in");
    Check(reloaded.Flag007(), "and the 007 flag");

    reloaded.SelectFolder(0);
    Check(!reloaded.Flag007() && !reloaded.IsCheatUnlocked(kLevelEgypt), "other folders are untouched");
    Check(GetSaveFlagSlot(*rig.manager->GetSaveForFoldernum(2)) == 1, "the wear-levelling counter moved on");
}

void TestApplyKeepsTheStoredChecksumsValid() {
    Rig rig;
    SaveEditor editor(rig.manager);
    editor.SetCheatUnlocked(kLevelDam, true);
    editor.Apply();

    // A restart resets any slot whose checksum fails, so a surviving edit
    // means the written slot checks out.
    rig.Start();
    const SaveData* save = rig.manager->GetSaveForFoldernum(0);
    Check(save && IsCheatUnlocked(*save, kLevelDam), "the written slot validates");
    Check(save && SaveChecksums(*save).chksum1 == save->chksum1 && SaveChecksums(*save).chksum2 == save->chksum2,
          "its checksums are the game's");
}

void TestCompletingImpliesTheEasierDifficulties() {
    Rig rig;
    SaveEditor editor(rig.manager);

    editor.SetStageCompleted(kLevelFacility, kDifficulty00, true);
    for (int32_t d = kDifficultyAgent; d <= kDifficulty00; d++) {
        Check(editor.IsStageCompleted(kLevelFacility, d), "every difficulty up to the chosen one is completed");
    }
    editor.SetStageTime(kLevelFacility, kDifficultyAgent, 120);
    Check(editor.StageTime(kLevelFacility, kDifficulty00) == SaveEditor::kMaxTime, "setting one time leaves others");

    editor.SetStageCompleted(kLevelFacility, kDifficultySecret, false);
    Check(editor.IsStageCompleted(kLevelFacility, kDifficultyAgent), "un-completing keeps the easier ones");
    Check(!editor.IsStageCompleted(kLevelFacility, kDifficultySecret) &&
              !editor.IsStageCompleted(kLevelFacility, kDifficulty00),
          "and clears the harder ones");
    Check(editor.StageTime(kLevelFacility, kDifficultyAgent) == 120, "the kept time is unchanged");
    Check(!editor.IsStageCompleted(kLevelDam, kDifficultyAgent) && !editor.IsStageCompleted(2, kDifficultyAgent),
          "other levels are untouched");
}

void TestTimesAreClampedToTheStoredRange() {
    Rig rig;
    SaveEditor editor(rig.manager);
    editor.SetStageTime(kLevelDam, kDifficultyAgent, 0);
    Check(editor.StageTime(kLevelDam, kDifficultyAgent) == SaveEditor::kMinTime, "zero is not a time");
    editor.SetStageTime(kLevelDam, kDifficultyAgent, 99999);
    Check(editor.StageTime(kLevelDam, kDifficultyAgent) == 0x3FF, "times over ten bits clamp");
    editor.SetStageTime(kLevelEgypt, kDifficulty00, 600);
    Check(editor.StageTime(kLevelEgypt, kDifficulty00) == 600, "the last level's time is stored whole");
    Check(editor.StageTime(kLevelDam, kDifficultyAgent) == 0x3FF, "and does not disturb its neighbours");
}

void TestOutOfRangeEditsAreIgnored() {
    Rig rig;
    SaveEditor editor(rig.manager);
    editor.SetStageCompleted(kSoloLevelCount, kDifficultyAgent, true);
    editor.SetStageCompleted(kLevelDam, kDifficulty007, true);
    editor.SetStageTime(-1, kDifficultyAgent, 10);
    editor.SetCheatUnlocked(kCheatCount, true);
    editor.SelectFolder(7);
    Check(!editor.IsDirty(), "nothing changed");
    Check(editor.Folder() == 0, "the folder stays put");
}

void TestCheatsLockAndUnlockPerBit() {
    Rig rig;
    SaveEditor editor(rig.manager);
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        editor.SetCheatUnlocked(level, true);
    }
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        Check(editor.IsCheatUnlocked(level), "every level's cheat unlocks");
    }
    editor.SetCheatUnlocked(9, false);
    editor.SetCheatUnlocked(17, false);
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        Check(editor.IsCheatUnlocked(level) == (level != 9 && level != 17), "only the locked ones lock");
    }
}

void TestEveryLevelHasARewardCheat() {
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        Check(std::string(RewardCheatName(level)) != "?", "each level names its cheat");
        Check(std::string(LevelName(level)) != "?", "and itself");
    }
}

void TestFlagsAndDerivedUnlocks() {
    Rig rig;
    SaveEditor editor(rig.manager);
    Check(!editor.Is007ModeUnlocked() && !editor.Flag007(), "007 mode starts locked");
    Check(!editor.CradleCompleted() && !editor.AztecCompletedOnSecretOr00() && !editor.EgyptCompletedOn00(),
          "so does the cast expansion");

    editor.SetStageCompleted(kLevelCradle, kDifficultyAgent, true);
    editor.SetStageCompleted(kLevelAztec, kDifficultySecret, true);
    editor.SetStageCompleted(kLevelEgypt, kDifficulty00, true);
    Check(editor.CradleCompleted() && editor.AztecCompletedOnSecretOr00() && editor.EgyptCompletedOn00(),
          "completions expand the cast");

    editor.CompleteAll(kDifficulty00);
    Check(editor.Is007ModeUnlocked() && !editor.Flag007(), "all levels on 00 Agent open 007 mode without the flag");
    editor.ClearAll(kDifficultyAgent);
    Check(!editor.IsStageCompleted(kLevelDam, kDifficulty00), "clearing a difficulty clears the harder ones");
    editor.SetFlag007(true);
    Check(editor.Is007ModeUnlocked(), "the flag opens it alone");
    editor.SetFlag007(false);
    Check(!editor.Flag007(), "and clears again");
}

void TestApplyFailsWithoutStorage() {
    Rig rig;
    SaveEditor editor(rig.manager);
    editor.SetCheatUnlocked(kLevelDam, true);
    rig.storage->SetPresent(false);
    Check(!editor.Apply(), "apply reports a missing EEPROM");
    Check(!editor.LastError().empty(), "with a reason");
    Check(editor.IsDirty(), "and keeps the edits");
}

void TestSelectingAFolderDropsEdits() {
    Rig rig;
    SaveEditor editor(rig.manager);
    editor.SetFlag007(true);
    editor.SelectFolder(1);
    Check(!editor.IsDirty() && !editor.Flag007(), "switching folders discards the edits");
    Check(GetSaveFolder(editor.Working()) == 1, "and loads the new folder's save");
}

} // namespace

int main() {
    TestEditsStayInTheCopyUntilApplied();
    TestApplyWritesThroughTheManagerAndSurvivesARestart();
    TestApplyKeepsTheStoredChecksumsValid();
    TestCompletingImpliesTheEasierDifficulties();
    TestTimesAreClampedToTheStoredRange();
    TestOutOfRangeEditsAreIgnored();
    TestCheatsLockAndUnlockPerBit();
    TestEveryLevelHasARewardCheat();
    TestFlagsAndDerivedUnlocks();
    TestApplyFailsWithoutStorage();
    TestSelectingAFolderDropsEdits();
    return gFailures == 0 ? 0 : 1;
}
