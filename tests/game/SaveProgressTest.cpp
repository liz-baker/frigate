#include "game/save/SaveProgress.h"

#include <cstdio>
#include <memory>

using namespace Frigate::Game::Save;

namespace {

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
    std::unique_ptr<SaveProgress> progress;

    Rig() {
        Start();
    }

    // A new run of the game over the same EEPROM.
    void Start() {
        manager = std::make_shared<SaveManager>(storage);
        manager->ValidateSaves();
        progress = std::make_unique<SaveProgress>(manager);
    }
};

int LiveSlots(const SaveManager& manager) {
    int live = 0;
    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        live += GetSaveFlagDoReset(manager.Slots()[i]) ? 0 : 1;
    }
    return live;
}

// Completes every level in [first, last] on one difficulty.
void CompleteLevels(SaveProgress& progress, int32_t folder, int32_t first, int32_t last, int32_t difficulty) {
    for (int32_t level = first; level <= last; level++) {
        progress.UnlockStage(folder, level, difficulty, 100 + level);
    }
}

void TestAFreshFolderOnlyOffersTheFirstLevel() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    for (int32_t difficulty = kDifficultyAgent; difficulty < kDifficultyCount; difficulty++) {
        Check(p.StageStatusFor(0, kLevelDam, difficulty) == StageStatus::Unlocked, "Dam is open on every difficulty");
        Check(p.StageStatusFor(0, kLevelFacility, difficulty) == StageStatus::Locked, "and nothing else is");
        Check(p.StageStatusFor(0, kLevelEgypt, difficulty) == StageStatus::Locked, "not even the bonus levels");
    }
    Check(p.HighestStageUnlocked(0) == kLevelDam, "the highest open level is Dam");
    Check(p.HighestStageUnlockedAnyFolder() == kLevelDam, "in any folder too");
    const StageDifficulty none = p.HighestStageDifficultyCompleted(0);
    Check(none.level == -1 && none.difficulty == kDifficultyMulti, "nothing is completed");
    Check(p.BestTime(0, kLevelDam, kDifficultyAgent) == 0, "so there is no best time");
    Check(!p.Is007ModeUnlocked(0), "and 007 is locked");
}

void TestCompletingALevelOpensTheNextOnTheSameDifficulty() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.UnlockStage(0, kLevelDam, kDifficultyAgent, 400);

    Check(p.StageStatusFor(0, kLevelDam, kDifficultyAgent) == StageStatus::Completed, "Dam is completed on Agent");
    Check(p.StageStatusFor(0, kLevelFacility, kDifficultyAgent) == StageStatus::Unlocked, "Facility opens on Agent");
    Check(p.StageStatusFor(0, kLevelFacility, kDifficultySecret) == StageStatus::Locked,
          "but not on the harder difficulties");
    Check(p.StageStatusFor(0, 2, kDifficultyAgent) == StageStatus::Locked, "and nothing past it");
    Check(p.StageStatusFor(0, kLevelDam, kDifficultySecret) == StageStatus::Unlocked,
          "Dam is still open on Secret Agent");
    Check(p.StageStatusFor(1, kLevelFacility, kDifficultyAgent) == StageStatus::Locked, "other folders are untouched");
    Check(p.HighestStageUnlocked(0) == kLevelFacility, "the highest open level moves on");
    Check(p.HighestStageUnlocked(1) == kLevelDam, "only in this folder");
    Check(p.HighestStageUnlockedAnyFolder() == kLevelFacility, "and any-folder sees it");
    const StageDifficulty done = p.HighestStageDifficultyCompleted(0);
    Check(done.level == kLevelDam && done.difficulty == kDifficultyAgent, "the highest completed is Dam on Agent");
}

void TestCompletingOnAHarderDifficultyCreditsTheEasierOnes() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.UnlockStage(2, 3, kDifficulty00, 250);

    Check(p.BestTime(2, 3, kDifficulty00) == 250, "the played difficulty keeps the real time");
    Check(p.BestTime(2, 3, kDifficultySecret) == 0x3FF && p.BestTime(2, 3, kDifficultyAgent) == 0x3FF,
          "the easier ones get the maximum time");
    Check(p.IsStageCompleted(2, 3, kDifficultyAgent) && p.IsStageCompleted(2, 3, kDifficultySecret),
          "so they read as completed");
    const StageDifficulty done = p.HighestStageDifficultyCompleted(2);
    Check(done.level == 3 && done.difficulty == kDifficulty00, "the harder difficulty ranks first");

    p.UnlockStage(2, kLevelEgypt - 1, kDifficultySecret, 90);
    const StageDifficulty again = p.HighestStageDifficultyCompleted(2);
    Check(again.difficulty == kDifficulty00 && again.level == 3, "difficulty outranks level when picking the highest");
}

void TestBestTimesOnlyImprove() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.UnlockStage(0, 5, kDifficultyAgent, 300);
    Check(p.BestTime(0, 5, kDifficultyAgent) == 300, "the first time is stored");
    p.UnlockStage(0, 5, kDifficultyAgent, 350);
    Check(p.BestTime(0, 5, kDifficultyAgent) == 300, "a slower time is ignored");
    p.UnlockStage(0, 5, kDifficultyAgent, 299);
    Check(p.BestTime(0, 5, kDifficultyAgent) == 299, "a faster one replaces it");
    p.UnlockStage(0, 5, kDifficultyAgent, 299);
    Check(p.BestTime(0, 5, kDifficultyAgent) == 299, "an equal one changes nothing");

    p.UnlockStage(0, 6, kDifficultyAgent, 5000);
    Check(p.BestTime(0, 6, kDifficultyAgent) == 0x3FF, "a time past ten bits is stored as the maximum");
    p.UnlockStage(0, 7, kDifficultyAgent, 0);
    Check(p.BestTime(0, 7, kDifficultyAgent) == 0x4F, "a zero time is stored as 0x4F so it still counts as completed");
    Check(p.IsStageCompleted(0, 7, kDifficultyAgent), "and the level reads as completed");

    p.UnlockStage(0, 5, kDifficultySecret, 200);
    Check(p.BestTime(0, 5, kDifficultyAgent) == 299, "a harder run does not beat an easier time that exists");
}

void TestUnlockIgnoresBadArguments() {
    Rig rig;
    SaveProgress& p = *rig.progress;
    const int32_t before = LiveSlots(*rig.manager);

    p.UnlockStage(-1, 0, 0, 10);
    p.UnlockStage(kFolderCount, 0, 0, 10);
    p.UnlockStage(0, -1, 0, 10);
    p.UnlockStage(0, kSoloLevelCount, 0, 10);
    p.UnlockStage(0, 0, -1, 10);
    p.UnlockStage(0, 0, kDifficultyCount, 10);

    Check(LiveSlots(*rig.manager) == before, "no slot changes");
    Check(p.HighestStageDifficultyCompleted(0).level == -1, "and nothing is completed");
    Check(p.StageStatusFor(0, kSoloLevelCount, 0) == StageStatus::Locked, "an unknown level is locked");
    Check(p.StageStatusFor(0, 0, kDifficultyCount) == StageStatus::Locked, "so is an unknown difficulty");
    Check(p.StageStatusFor(9, 0, 0) == StageStatus::Locked, "and an unknown folder");
    Check(p.BestTime(-1, 0, 0) == 0 && p.BestTime(9, 0, 0) == 0, "which have no times either");
}

void TestTheBonusLevelsAreGated() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    CompleteLevels(p, 0, kLevelDam, kLevelCradle, kDifficultyAgent);
    Check(p.StageStatusFor(0, kLevelAztec, kDifficultyAgent) == StageStatus::Locked, "Aztec is never open on Agent");
    Check(p.StageStatusFor(0, kLevelEgypt, kDifficultySecret) == StageStatus::Locked, "nor Egypt below 00 Agent");
    Check(p.IsCradleCompleted(0), "Cradle is completed");
    Check(p.IsCradleCompletedAnyFolder() && !p.IsCradleCompleted(1), "in this folder only");

    CompleteLevels(p, 0, kLevelDam, kLevelCradle, kDifficultySecret);
    Check(p.StageStatusFor(0, kLevelAztec, kDifficultySecret) == StageStatus::Unlocked,
          "Aztec opens on Secret Agent once the rest are done there");
    Check(p.StageStatusFor(0, kLevelEgypt, kDifficulty00) == StageStatus::Locked, "Egypt still wants 00 Agent");
    Check(p.HighestStageUnlocked(0) == kLevelAztec, "so Aztec is the highest open level");

    p.UnlockStage(0, kLevelAztec, kDifficultySecret, 500);
    Check(p.IsAztecCompletedOnSecretOr00(0) && p.IsAztecCompletedOnSecretOr00AnyFolder(), "Aztec is completed");
    Check(!p.IsAztecCompletedOnSecretOr00(1), "only in folder 0");
    Check(!p.IsEgyptCompletedOn00AnyFolder(), "Egypt is not");

    CompleteLevels(p, 0, kLevelDam, kLevelAztec, kDifficulty00);
    Check(p.StageStatusFor(0, kLevelEgypt, kDifficulty00) == StageStatus::Unlocked, "Egypt opens on 00 Agent");
    p.UnlockStage(0, kLevelEgypt, kDifficulty00, 600);
    Check(p.IsEgyptCompletedOn00(0) && p.IsEgyptCompletedOn00AnyFolder(), "and can be completed");
    Check(p.HighestStageUnlocked(0) == kLevelEgypt, "the highest open level is the last");
    Check(p.Is007ModeUnlocked(0), "completing every level on 00 Agent unlocks 007");
    Check(p.BestTime(0, kLevelDam, kDifficulty007) == 0x3FF, "whose levels then read as completed");
    Check(p.StageStatusFor(0, kLevelEgypt, kDifficulty007) == StageStatus::Completed, "on 007 mode");
    const StageDifficulty done = p.HighestStageDifficultyCompleted(0);
    Check(done.level == kLevelEgypt && done.difficulty == kDifficulty007, "007 is the highest difficulty completed");
}

void TestTheDebugFlagsOpenLevelsInAnEmptyFolder() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    rig.manager->SetDebugEnableAgentLevels(true);
    Check(p.StageStatusFor(0, 7, kDifficultyAgent) == StageStatus::Unlocked, "every level opens on Agent");
    Check(p.StageStatusFor(0, 7, kDifficultySecret) == StageStatus::Locked, "but not on the others");

    rig.manager->SetDebugEnableAgentLevels(false);
    rig.manager->SetDebugEnableAllLevels(true);
    Check(p.StageStatusFor(0, kLevelEgypt, kDifficulty007) == StageStatus::Unlocked, "all-levels opens everything");
    Check(p.StageStatusFor(0, kSoloLevelCount, kDifficulty007) == StageStatus::Locked,
          "except levels that do not exist");
    rig.manager->SetDebugEnableAllLevels(false);
    Check(p.StageStatusFor(0, 7, kDifficultyAgent) == StageStatus::Locked, "and both start off");
}

void TestCheatUnlocks() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    Check(!p.IsCheatUnlocked(1, 4), "cheats start locked");
    p.UnlockCheat(1, 4);
    p.UnlockCheat(1, 19);
    Check(p.IsCheatUnlocked(1, 4) && p.IsCheatUnlocked(1, 19), "unlocked cheats read back");
    Check(!p.IsCheatUnlocked(1, 5) && !p.IsCheatUnlocked(0, 4), "and only those, only there");
    Check(!p.IsCheatUnlocked(1, -1) && !p.IsCheatUnlocked(1, kCheatCount), "an unknown cheat is not unlocked");
    Check(!p.IsCheatUnlocked(-1, 4) && !p.IsCheatUnlocked(7, 4), "nor is one in an unknown folder");

    p.UnlockCheat(kFolderCount, 4);
    p.UnlockCheat(0, kCheatCount);
    p.UnlockCheat(0, -1);
    Check(!p.IsCheatUnlocked(0, 4), "bad arguments unlock nothing");
    Check(LiveSlots(*rig.manager) == 4, "and the folders keep their four slots");
}

void TestProgressSurvivesARestart() {
    Rig rig;
    rig.progress->UnlockStage(0, kLevelDam, kDifficultyAgent, 123);
    rig.progress->UnlockStage(0, kLevelFacility, kDifficultySecret, 456);
    rig.progress->UnlockStage(3, 10, kDifficulty00, 789);
    rig.progress->UnlockCheat(0, 7);
    rig.progress->UnlockCheat(3, 0);

    rig.Start();
    SaveProgress& p = *rig.progress;

    Check(p.BestTime(0, kLevelDam, kDifficultyAgent) == 123, "a best time is still there");
    Check(p.BestTime(0, kLevelFacility, kDifficultySecret) == 456, "and another");
    Check(p.BestTime(3, 10, kDifficulty00) == 789 && p.BestTime(3, 10, kDifficultyAgent) == 0x3FF,
          "with the easier difficulties' credit");
    Check(p.StageStatusFor(0, 2, kDifficultyAgent) == StageStatus::Unlocked, "unlocks follow from the stored times");
    Check(p.IsCheatUnlocked(0, 7) && p.IsCheatUnlocked(3, 0) && !p.IsCheatUnlocked(3, 7), "as do cheats");
    Check(p.HighestStageUnlockedAnyFolder() == 11, "and the highest level open anywhere");
    Check(LiveSlots(*rig.manager) == 4, "on four live slots");
}

void TestDeletingAFolder() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.UnlockStage(1, 2, kDifficultyAgent, 200);
    p.UnlockStage(2, 2, kDifficultyAgent, 210);
    p.UnlockCheat(1, 3);
    SaveData* folder1 = rig.manager->GetSaveForFoldernum(1);
    SaveData tuned = *folder1;
    tuned.musicVol = 0x20;
    rig.manager->OverwriteSaveSlotWithNewSave(folder1, tuned);

    p.DeleteFolder(1);
    Check(!p.IsStageCompleted(1, 2, kDifficultyAgent), "the folder's times are gone");
    Check(p.IsStageCompleted(2, 2, kDifficultyAgent), "another folder's are not");
    const SaveData* blank = rig.manager->GetSaveForFoldernum(1);
    Check(blank != nullptr && GetSaveFolder(*blank) == 1 && !GetSaveFlagDoReset(*blank), "the folder stays in use");
    Check(blank->musicVol == 0xFF, "with the settings back at their defaults");
    Check(GetSelectedBond(*blank) == 1, "and the folder number as its bond slot");

    rig.Start();
    Check(!rig.progress->IsStageCompleted(1, 2, kDifficultyAgent) && rig.progress->IsStageCompleted(2, 2, 0),
          "a deleted folder stays deleted");

    // A folder with nothing completed is left exactly as it is.
    rig.progress->UnlockCheat(0, 5);
    const SaveData kept = *rig.manager->GetSaveForFoldernum(0);
    rig.progress->DeleteFolder(0);
    Check(*rig.manager->GetSaveForFoldernum(0) == kept, "so its cheats survive");
    rig.progress->DeleteFolder(-1);
    rig.progress->DeleteFolder(kFolderCount);
    Check(LiveSlots(*rig.manager) == 4, "bad folder numbers do nothing");
}

void TestCopyingAFolder() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.UnlockStage(0, 4, kDifficultySecret, 321);
    p.UnlockCheat(0, 6);
    p.UnlockCheat(2, 1);
    p.UnlockStage(2, 0, kDifficultyAgent, 99);

    // Folder 1 is empty, so it is the first free.
    p.CopyFolderToFirstFree(0);
    Check(p.BestTime(1, 4, kDifficultySecret) == 321, "the times are copied");
    Check(p.IsCheatUnlocked(1, 6), "and the cheats");
    const SaveData* copy = rig.manager->GetSaveForFoldernum(1);
    Check(copy != nullptr && GetSaveFolder(*copy) == 1, "under the target's own folder number");
    Check(p.BestTime(0, 4, kDifficultySecret) == 321, "the source is unchanged");

    // 0, 1 and 2 are in use; the next copy lands in folder 3.
    p.CopyFolderToFirstFree(2);
    Check(p.IsStageCompleted(3, 0, kDifficultyAgent) && p.IsCheatUnlocked(3, 1), "the next copy takes folder 3");

    // All four in use: nothing to overwrite.
    p.UnlockStage(3, 9, kDifficultyAgent, 9);
    p.CopyFolderToFirstFree(3);
    Check(p.BestTime(0, 9, kDifficultyAgent) == 0 && p.BestTime(1, 9, kDifficultyAgent) == 0 &&
              p.BestTime(2, 9, kDifficultyAgent) == 0,
          "with no free folder nothing is copied");

    rig.Start();
    Check(rig.progress->BestTime(1, 4, kDifficultySecret) == 321 && rig.progress->IsCheatUnlocked(3, 1),
          "copies persist");
    Check(LiveSlots(*rig.manager) == 4, "and use one slot per folder");
}

void TestCopyingAnEmptyFolderDoesNothing() {
    Rig rig;
    rig.progress->UnlockCheat(0, 2);
    const SaveData before = *rig.manager->GetSaveForFoldernum(1);
    rig.progress->CopyFolderToFirstFree(0);
    Check(*rig.manager->GetSaveForFoldernum(1) == before, "a folder with nothing completed is not copied");
    rig.progress->CopyFolderToFirstFree(-1);
    rig.progress->CopyFolderToFirstFree(kFolderCount);
    Check(!rig.progress->IsCheatUnlocked(1, 2), "and bad folder numbers do nothing");
}

void TestInitializingAllTimes() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.UnlockStage(0, 3, kDifficulty00, 77);
    p.InitializeAllTimes(0);

    Check(p.BestTime(0, 3, kDifficulty00) == 77, "an existing time is kept");
    Check(p.BestTime(0, 0, kDifficultyAgent) == 0x3FF && p.BestTime(0, kLevelEgypt, kDifficulty00) == 0x3FF,
          "every other level gets the maximum time");
    Check(rig.manager->GetStageCompletedForDifficulty(*rig.manager->GetSaveForFoldernum(0), 5, kDifficultySecret),
          "on all three difficulties");
    Check(p.Is007ModeUnlocked(0), "which unlocks 007 mode");
    Check(!p.Is007ModeUnlocked(1), "in that folder only");
    p.InitializeAllTimes(-1);
    p.InitializeAllTimes(9);
}

void TestCurrentFolderAndDifficulty() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    Check(p.CurrentFolder() == 0 && p.SelectedDifficulty() == kDifficultyMulti, "the cold-boot selection");
    p.SetCurrentFolder(2);
    Check(p.CurrentFolder() == 2, "the current folder is stored");

    p.SetSelectedDifficulty(kDifficulty00);
    Check(p.SelectedDifficulty() == kDifficulty00, "a difficulty is stored");
    p.SetSelectedDifficulty(kDifficulty007);
    Check(p.SelectedDifficulty() == kDifficulty007, "including 007");
    p.SetSelectedDifficulty(kDifficultySecret);
    Check(p.SelectedDifficulty() == kDifficultySecret, "and Secret Agent");
    p.SetSelectedDifficulty(kDifficultyMulti);
    Check(p.SelectedDifficulty() == kDifficultyAgent, "anything else becomes Agent");
    p.SetSelectedDifficulty(9);
    Check(p.SelectedDifficulty() == kDifficultyAgent, "including out-of-range values");

    p.UnlockStage(2, 0, kDifficultyAgent, 50);
    const SaveData copy = p.CopyCurrentFolderSave();
    Check(copy == *rig.manager->GetSaveForFoldernum(2), "the current folder's save can be copied");
    p.SetCurrentFolder(9);
    Check(p.CopyCurrentFolderSave() == BlankSave(), "a folder without a save copies as blank");

    p.SetCurrentFolder(1);
    Check(p.BondForCurrentFolder() == kBondBrosnan, "the bond is Brosnan");
    const SaveData before = *rig.manager->GetSaveForFoldernum(1);
    p.UpdateBondInCurrentFolder();
    Check(*rig.manager->GetSaveForFoldernum(1) == before, "and an unchanged bond writes nothing");
}

void TestSelectingTheDemoFolder() {
    Rig rig;
    SaveProgress& p = *rig.progress;

    p.SetCurrentFolder(3);
    SaveData demo = BlankSave();
    SetSaveFlagDoReset(demo, false);
    rig.manager->CheckStageDifficultyTime(demo, 0, kDifficultyAgent, 60);
    p.SelectDemoFolder(demo);

    Check(p.CurrentFolder() == kRamRomFolderNum && p.PreviousFolder() == 3, "the demo folder is selected");
    const SaveData* ramrom = rig.manager->GetSaveForFoldernum(kRamRomFolderNum);
    Check(ramrom != nullptr && *ramrom == demo, "holding the recording's save");
    Check(p.CopyCurrentFolderSave() == demo, "which the current-folder copy reads");
}

void TestDeleteCurrentFolderSavesTheSettings() {
    Rig rig;
    SaveProgress& p = *rig.progress;
    p.SetCurrentFolder(1);

    const SaveData before = *rig.manager->GetSaveForFoldernum(1);
    p.DeleteCurrentSelectedFolder();
    Check(*rig.manager->GetSaveForFoldernum(1) == before, "without a settings snapshot the folder is left alone");

    p.SetSettingsCapture([](SaveData& save) {
        save.musicVol = 0x30;
        save.options = 0x0001;
    });
    p.DeleteCurrentSelectedFolder();
    const SaveData* saved = rig.manager->GetSaveForFoldernum(1);
    Check(saved->musicVol == 0x30 && saved->options == 0x0001, "changed settings are written to the folder");
    Check(GetSaveFlagSlot(*saved) == 1, "through a slot rewrite");

    const SaveData stable = *saved;
    p.DeleteCurrentSelectedFolder();
    Check(*rig.manager->GetSaveForFoldernum(1) == stable, "unchanged settings write nothing");

    rig.Start();
    Check(rig.manager->GetSaveForFoldernum(1)->musicVol == 0x30, "and they persist");

    // Progress is kept: only the settings are replaced.
    rig.progress->UnlockStage(1, 2, kDifficultyAgent, 33);
    rig.progress->SetSettingsCapture([](SaveData& save) { save.sfxVol = 0x11; });
    rig.progress->SetCurrentFolder(1);
    rig.progress->DeleteCurrentSelectedFolder();
    Check(rig.progress->IsStageCompleted(1, 2, kDifficultyAgent) && rig.manager->GetSaveForFoldernum(1)->sfxVol == 0x11,
          "times survive a settings save");
    rig.progress->SetCurrentFolder(9);
    rig.progress->DeleteCurrentSelectedFolder();
    Check(LiveSlots(*rig.manager) == 4, "an unknown folder does nothing");
}

void TestUpdatingTheBondInTheSave() {
    Rig rig;
    // Validation loads each folder's bond from its save, so the two agree
    // until the save is changed behind it; then the save is rewritten.
    SaveData* folder = rig.manager->GetSaveForFoldernum(2);
    SaveData edited = *folder;
    SetSelectedBond(edited, kBondConnery);
    rig.manager->OverwriteSaveSlotWithNewSave(folder, edited);
    Check(GetSelectedBond(*rig.manager->GetSaveForFoldernum(2)) == kBondConnery, "a save can hold another bond");

    rig.manager->UpdateSelectedBondInSave(2);
    Check(GetSelectedBond(*rig.manager->GetSaveForFoldernum(2)) == static_cast<uint32_t>(rig.manager->SelectedBond(2)),
          "the save follows the folder's selected bond");
    rig.manager->UpdateSelectedBondInSave(-1);
    rig.manager->UpdateSelectedBondInSave(kFolderCount);
    Check(LiveSlots(*rig.manager) == 4, "bad folder numbers do nothing");
}

} // namespace

int main() {
    TestAFreshFolderOnlyOffersTheFirstLevel();
    TestCompletingALevelOpensTheNextOnTheSameDifficulty();
    TestCompletingOnAHarderDifficultyCreditsTheEasierOnes();
    TestBestTimesOnlyImprove();
    TestUnlockIgnoresBadArguments();
    TestTheBonusLevelsAreGated();
    TestTheDebugFlagsOpenLevelsInAnEmptyFolder();
    TestCheatUnlocks();
    TestProgressSurvivesARestart();
    TestDeletingAFolder();
    TestCopyingAFolder();
    TestCopyingAnEmptyFolderDoesNothing();
    TestInitializingAllTimes();
    TestCurrentFolderAndDifficulty();
    TestSelectingTheDemoFolder();
    TestDeleteCurrentFolderSavesTheSettings();
    TestUpdatingTheBondInTheSave();
    return gFailures == 0 ? 0 : 1;
}
