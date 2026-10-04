#include "game/save/SaveManager.h"

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

constexpr size_t kSlotOffset = 32;

size_t SlotAt(size_t slot) {
    return kSlotOffset + slot * kSaveDataSize;
}

SaveBytes SlotBytes(const MemorySaveStorage& storage, size_t slot) {
    SaveBytes bytes;
    for (size_t i = 0; i < kSaveDataSize; i++) {
        bytes[i] = storage.Image()[SlotAt(slot) + i];
    }
    return bytes;
}

struct Rig {
    std::shared_ptr<MemorySaveStorage> storage = std::make_shared<MemorySaveStorage>();
    std::unique_ptr<SaveManager> manager = std::make_unique<SaveManager>(storage);

    // The next run of the game: a new manager over the same EEPROM.
    void Restart() {
        manager = std::make_unique<SaveManager>(storage);
        manager->ValidateSaves();
    }
};

bool AllFoldersBlank(const SaveManager& manager) {
    for (uint32_t folder = 0; folder < kFolderCount; folder++) {
        const SaveData* save = manager.GetSaveForFoldernum(folder);
        if (save == nullptr) {
            return false;
        }
        SaveData expected = BlankSave();
        SetSaveFolder(expected, folder);
        SetSaveFlagDoReset(expected, false);
        SetSelectedBond(expected, static_cast<int32_t>(folder));
        expected.chksum1 = save->chksum1;
        expected.chksum2 = save->chksum2;
        if (!(*save == expected)) {
            return false;
        }
    }
    return true;
}

int LiveSlots(const SaveManager& manager) {
    int live = 0;
    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        live += GetSaveFlagDoReset(manager.Slots()[i]) ? 0 : 1;
    }
    return live;
}

void TestAMissingSaveBecomesFourBlankFolders() {
    Rig rig;
    rig.manager->ValidateSaves();

    Check(AllFoldersBlank(*rig.manager), "each folder gets a blank save of its own");
    Check(LiveSlots(*rig.manager) == 4, "four of the five slots are live");
    Check(rig.manager->GetSaveFlagDoResetAnyFolder() == 4, "the spare is the fifth slot");
    Check(rig.storage->HasImage(), "and the chip has been written");

    const auto& image = rig.storage->Image();
    Check(image[8] == 0x42, "the header carries the marker byte");
    const Checksums header = GenerateChecksums(image.data() + 8, image.data() + 32);
    Check(static_cast<int32_t>((image[0] << 24) | (image[1] << 16) | (image[2] << 8) | image[3]) == header.chksum1,
          "and its checksum");

    for (size_t slot = 0; slot < kEepromSaveSlots; slot++) {
        const SaveData stored = DeserializeSave(SlotBytes(*rig.storage, slot));
        const Checksums sums = SaveChecksums(stored);
        Check(stored.chksum1 == sums.chksum1 && stored.chksum2 == sums.chksum2,
              "every slot is stored with a valid checksum");
        Check(stored == rig.manager->Slots()[slot], "and matches the manager's copy");
    }
}

void TestAnAllZeroChipRecovers() {
    Rig rig;
    rig.storage->Load({});
    rig.manager->ValidateSaves();
    Check(AllFoldersBlank(*rig.manager), "a zeroed chip fails every check and comes back blank");
}

void TestAnErasedChipRecovers() {
    Rig rig;
    std::array<uint8_t, kEepromSize> erased;
    erased.fill(0xFF);
    rig.storage->Load(erased);
    rig.manager->ValidateSaves();
    Check(AllFoldersBlank(*rig.manager), "so does an erased one");
}

void TestAValidChipIsLeftAlone() {
    Rig rig;
    rig.manager->ValidateSaves();
    const auto image = rig.storage->Image();
    const int writes = rig.storage->WriteCount();

    rig.Restart();
    Check(rig.storage->WriteCount() == writes, "validating a sound chip writes nothing");
    Check(rig.storage->Image() == image, "and changes nothing");
    Check(AllFoldersBlank(*rig.manager), "the folders load as they were");
}

void TestChangesSurviveARestart() {
    Rig rig;
    rig.manager->ValidateSaves();

    rig.manager->SaveFolderUnlockCheat(1, 3);
    rig.manager->SaveFolderUnlockCheat(1, 17);
    rig.manager->SaveFolderUnlockCheat(3, 0);

    SaveData* folder2 = rig.manager->GetSaveForFoldernum(2);
    SaveData edited = *folder2;
    rig.manager->CheckStageDifficultyTime(edited, 4, kDifficultySecret, 321);
    rig.manager->CheckStageDifficultyTime(edited, 19, kDifficulty00, 1000);
    edited.musicVol = 0x40;
    edited.sfxVol = 0x20;
    edited.options = 0x0B2D;
    edited.flag007 = 1;
    rig.manager->OverwriteSaveSlotWithNewSave(folder2, edited);

    std::array<SaveData, 4> before;
    for (uint32_t folder = 0; folder < kFolderCount; folder++) {
        before[folder] = *rig.manager->GetSaveForFoldernum(folder);
    }

    rig.Restart();
    for (uint32_t folder = 0; folder < kFolderCount; folder++) {
        const SaveData* after = rig.manager->GetSaveForFoldernum(folder);
        Check(after != nullptr && *after == before[folder], "a folder reads back identically in the next run");
    }
    Check(IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(1), 3) &&
              IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(1), 17) &&
              !IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(1), 0),
          "cheats stay unlocked in their own folder");
    Check(IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(3), 0) &&
              !IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(0), 0),
          "and only there");
    const SaveData* reloaded = rig.manager->GetSaveForFoldernum(2);
    Check(rig.manager->GetStageDifficultyTime(*reloaded, 4, kDifficultySecret) == 321 &&
              rig.manager->GetStageDifficultyTime(*reloaded, 19, kDifficulty00) == 0x3E8,
          "best times persist");
    Check(reloaded->musicVol == 0x40 && reloaded->sfxVol == 0x20 && reloaded->options == 0x0B2D &&
              reloaded->flag007 == 1,
          "as do the volumes, options and the 007 flag");
    Check(LiveSlots(*rig.manager) == 4, "and the folders still use four slots");
}

void TestEachChangeMovesTheFolderToTheSpareSlot() {
    Rig rig;
    rig.manager->ValidateSaves();

    const SaveData* before = rig.manager->GetSaveForFoldernum(0);
    Check(before == &rig.manager->Slots()[0], "folder 1 starts in the first slot");
    rig.manager->SaveFolderUnlockCheat(0, 1);
    const SaveData* after = rig.manager->GetSaveForFoldernum(0);
    Check(after == &rig.manager->Slots()[4], "an update lands in the spare slot");
    Check(GetSaveFlagSlot(*after) == 1, "with the counter one ahead");
    Check(GetSaveFlagDoReset(rig.manager->Slots()[0]), "and the old slot is free");
    Check(rig.manager->GetSaveFlagDoResetAnyFolder() == 0, "which is now the spare");

    for (int32_t cheat = 2; cheat < 6; cheat++) {
        rig.manager->SaveFolderUnlockCheat(0, cheat);
    }
    Check(GetSaveFlagSlot(*rig.manager->GetSaveForFoldernum(0)) == 1,
          "five updates bring the counter round to one again");
    Check(IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(0), 1) &&
              IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(0), 5),
          "with the cheats kept through the moves");
    rig.Restart();
    Check(IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(0), 5) && LiveSlots(*rig.manager) == 4,
          "and the result loads");
}

void TestUnlockingACheatTwiceWritesOnce() {
    Rig rig;
    rig.manager->ValidateSaves();
    rig.manager->SaveFolderUnlockCheat(0, 4);
    const int writes = rig.storage->WriteCount();
    const SaveData* where = rig.manager->GetSaveForFoldernum(0);
    rig.manager->SaveFolderUnlockCheat(0, 4);
    Check(rig.storage->WriteCount() == writes, "an unlocked cheat costs no write");
    Check(rig.manager->GetSaveForFoldernum(0) == where, "and the folder stays where it was");
}

void TestCheatUnlockIgnoresBadArguments() {
    Rig rig;
    rig.manager->ValidateSaves();
    const int writes = rig.storage->WriteCount();
    rig.manager->SaveFolderUnlockCheat(-1, 0);
    rig.manager->SaveFolderUnlockCheat(kFolderCount, 0);
    rig.manager->SaveFolderUnlockCheat(kRamRomFolderNum, 0);
    rig.manager->SaveFolderUnlockCheat(0, -1);
    rig.manager->SaveFolderUnlockCheat(0, kCheatCount);
    Check(rig.storage->WriteCount() == writes && AllFoldersBlank(*rig.manager),
          "a bad folder or cheat number changes nothing");
}

void TestACorruptSlotResetsOnlyItsFolder() {
    Rig rig;
    rig.manager->ValidateSaves();
    rig.manager->SaveFolderUnlockCheat(0, 2);
    rig.manager->SaveFolderUnlockCheat(1, 5);
    rig.manager->SaveFolderUnlockCheat(2, 6);

    const SaveData folder1 = *rig.manager->GetSaveForFoldernum(1);
    const SaveData folder3 = *rig.manager->GetSaveForFoldernum(3);
    const size_t damaged = static_cast<size_t>(rig.manager->GetSaveForFoldernum(2) - rig.manager->Slots().data());

    auto image = rig.storage->Image();
    image[SlotAt(damaged) + 40] ^= 0x10;
    rig.storage->Load(image);
    rig.Restart();

    const SaveData* hurt = rig.manager->GetSaveForFoldernum(2);
    Check(hurt != nullptr && !IsCheatUnlocked(*hurt, 6), "the damaged folder comes back empty");
    SaveData expected = BlankSave();
    SetSaveFolder(expected, 2);
    SetSaveFlagDoReset(expected, false);
    SetSelectedBond(expected, 2);
    expected.chksum1 = hurt->chksum1;
    expected.chksum2 = hurt->chksum2;
    Check(*hurt == expected, "as a fresh folder 3");
    Check(*rig.manager->GetSaveForFoldernum(1) == folder1 && *rig.manager->GetSaveForFoldernum(3) == folder3,
          "its neighbours are untouched");
    Check(IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(1), 5), "including their cheats");
    Check(LiveSlots(*rig.manager) == 4, "and there are still four live slots");

    rig.Restart();
    Check(!IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(2), 6), "the repair was written back");
}

void TestEveryByteOfASlotIsProtected() {
    Rig rig;
    rig.manager->ValidateSaves();
    rig.manager->SaveFolderUnlockCheat(0, 9);
    const auto good = rig.storage->Image();
    const size_t live = static_cast<size_t>(rig.manager->GetSaveForFoldernum(0) - rig.manager->Slots().data());

    bool allCaught = true;
    for (size_t i = 0; i < kSaveDataSize; i++) {
        auto image = good;
        image[SlotAt(live) + i] ^= 0x01;
        rig.storage->Load(image);
        rig.Restart();
        const SaveData* folder = rig.manager->GetSaveForFoldernum(0);
        allCaught &= folder != nullptr && !IsCheatUnlocked(*folder, 9);
    }
    Check(allCaught, "a flipped bit anywhere in a stored slot, checksums and padding included, resets it");
}

void TestACorruptHeaderIsRewrittenAndKeepsTheSaves() {
    Rig rig;
    rig.manager->ValidateSaves();
    rig.manager->SaveFolderUnlockCheat(1, 12);
    const SaveData folder = *rig.manager->GetSaveForFoldernum(1);

    auto image = rig.storage->Image();
    image[20] ^= 0x01;
    rig.storage->Load(image);
    rig.Restart();
    Check(rig.storage->Image()[20] == 0, "a bad header is rewritten");
    Check(rig.storage->Image()[8] == 0x42, "with the marker");
    Check(*rig.manager->GetSaveForFoldernum(1) == folder, "the folders are not disturbed by it");

    image = rig.storage->Image();
    image[8] = 0x43;
    rig.storage->Load(image);
    rig.Restart();
    Check(rig.storage->Image()[8] == 0x42, "a wrong marker byte counts as a bad header");
    Check(*rig.manager->GetSaveForFoldernum(1) == folder, "and again leaves the folders alone");
}

SaveData LiveFolder(uint32_t folder, int32_t counter, int32_t cheat) {
    SaveData save = BlankSave();
    SetSaveFolder(save, folder);
    SetSaveFlagDoReset(save, false);
    SetSaveFlagSlot(save, counter);
    SetCheatUnlocked(save, cheat);
    const Checksums sums = SaveChecksums(save);
    save.chksum1 = sums.chksum1;
    save.chksum2 = sums.chksum2;
    return save;
}

// An EEPROM image with the given slots (the rest free) and a sound header.
std::array<uint8_t, kEepromSize> ImageWith(const std::array<SaveData, 5>& slots) {
    Rig scratch;
    scratch.manager->ValidateSaves();
    auto image = scratch.storage->Image();
    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        SaveData save = slots[i];
        const Checksums sums = SaveChecksums(save);
        save.chksum1 = sums.chksum1;
        save.chksum2 = sums.chksum2;
        const SaveBytes bytes = SerializeSave(save);
        for (size_t b = 0; b < kSaveDataSize; b++) {
            image[SlotAt(i) + b] = bytes[b];
        }
    }
    return image;
}

SaveData FreeSlot() {
    return BlankSave();
}

void TestAnInterruptedUpdateKeepsTheNewerCopy() {
    // The update wrote the new copy (counter one ahead) but didn't free the old.
    Rig rig;
    rig.storage->Load(ImageWith(
        { LiveFolder(0, 1, 1), LiveFolder(1, 0, 0), LiveFolder(2, 0, 0), LiveFolder(3, 0, 0), LiveFolder(0, 2, 2) }));
    rig.manager->ValidateSaves();
    const SaveData* kept = rig.manager->GetSaveForFoldernum(0);
    Check(kept == &rig.manager->Slots()[4] && GetSaveFlagSlot(*kept) == 2 && IsCheatUnlocked(*kept, 2),
          "of two copies of a folder the one a step ahead is kept");
    Check(GetSaveFlagDoReset(rig.manager->Slots()[0]), "and the other is freed");
    Check(LiveSlots(*rig.manager) == 4, "leaving four live folders");

    // The counter wraps from 3 to 0.
    Rig wrap;
    wrap.storage->Load(ImageWith(
        { LiveFolder(0, 3, 1), LiveFolder(1, 0, 0), LiveFolder(2, 0, 0), LiveFolder(3, 0, 0), LiveFolder(0, 0, 2) }));
    wrap.manager->ValidateSaves();
    Check(wrap.manager->GetSaveForFoldernum(0) == &wrap.manager->Slots()[4], "counter 0 is newer than counter 3");

    // Counters that aren't consecutive: the first copy found wins.
    Rig odd;
    odd.storage->Load(ImageWith(
        { LiveFolder(0, 0, 1), LiveFolder(1, 0, 0), LiveFolder(2, 0, 0), LiveFolder(3, 0, 0), LiveFolder(0, 2, 2) }));
    odd.manager->ValidateSaves();
    Check(odd.manager->GetSaveForFoldernum(0) == &odd.manager->Slots()[0], "otherwise the first copy stands");
    Check(GetSaveFlagDoReset(odd.manager->Slots()[4]), "and the later one is freed");

    // A folder with no copy gets a new one in a free slot.
    Rig missing;
    missing.storage->Load(
        ImageWith({ LiveFolder(0, 0, 0), FreeSlot(), LiveFolder(2, 0, 0), LiveFolder(3, 0, 0), FreeSlot() }));
    missing.manager->ValidateSaves();
    Check(missing.manager->GetSaveForFoldernum(1) != nullptr && LiveSlots(*missing.manager) == 4,
          "a folder that has gone missing is rebuilt");
}

void TestStorageThatCannotReadFallsBack() {
    class Unreadable : public MemorySaveStorage {
      public:
        bool Read(uint8_t, uint8_t*, size_t) override {
            return false;
        }
    };
    auto storage = std::make_shared<Unreadable>();
    SaveManager manager(storage);
    manager.ValidateSaves();
    Check(AllFoldersBlank(manager), "an unreadable chip gives four blank folders");
    Check(storage->WriteCount() > 0, "and they are written out");
}

void TestStorageThatCannotWriteStillWorksInMemory() {
    Rig rig;
    rig.storage->SetWriteFails(true);
    rig.manager->ValidateSaves();
    Check(AllFoldersBlank(*rig.manager), "folders exist even when nothing can be saved");
    rig.manager->SaveFolderUnlockCheat(0, 3);
    Check(IsCheatUnlocked(*rig.manager->GetSaveForFoldernum(0), 3), "and edits stick for the session");
    Check(!rig.storage->HasImage(), "though nothing reached the chip");
}

void TestNoEepromMeansNoSaving() {
    Rig rig;
    rig.storage->SetPresent(false);
    rig.manager->ValidateSaves();
    Check(rig.manager->Slots()[0] == SaveData{}, "validation does nothing without an EEPROM");
    rig.manager->SaveFolderUnlockCheat(0, 3);
    Check(rig.storage->WriteCount() == 0 && !rig.storage->HasImage(), "and nothing is written");
}

void TestTheManagerStartsAsUninitializedSlots() {
    Rig rig;
    for (size_t i = 0; i < kSaveSlotCount; i++) {
        Check(rig.manager->Slots()[i] == SaveData{}, "slots are zeroed until validated");
    }
}

void TestFolderLookupAndValidity() {
    Rig rig;
    rig.manager->ValidateSaves();
    Check(SaveManager::IsFolderValid(0) && SaveManager::IsFolderValid(3) &&
              SaveManager::IsFolderValid(kRamRomFolderNum),
          "folders 0-3 and the demo folder are valid");
    Check(!SaveManager::IsFolderValid(-1) && !SaveManager::IsFolderValid(4) && !SaveManager::IsFolderValid(0x65),
          "nothing else is");
    Check(rig.manager->GetSaveForFoldernum(4) == nullptr && rig.manager->GetSaveForFoldernum(0xFFFFFFFFu) == nullptr,
          "an unknown folder has no save");
    Check(rig.manager->GetSaveForFoldernum(kRamRomFolderNum) == &rig.manager->Slots()[kSaveSlotRamRom],
          "the demo folder is the sixth slot");
}

void TestCopySave() {
    Rig rig;
    rig.manager->ValidateSaves();
    rig.manager->SaveFolderUnlockCheat(1, 8);
    Check(IsCheatUnlocked(rig.manager->CopySave(1), 8), "a copy has the folder's contents");
    Check(rig.manager->CopySave(9) == BlankSave(), "a folder with no save copies as blank");
}

void TestTheDemoFolderIsMemoryOnly() {
    Rig rig;
    rig.manager->ValidateSaves();
    const int writes = rig.storage->WriteCount();

    SaveData demo = BlankSave();
    demo.options = 0x1234;
    rig.manager->CopyDemoSaveToRamRomSave(0, demo);
    Check(rig.manager->GetSaveForFoldernum(kRamRomFolderNum)->options == 0,
          "copying to any other folder number is ignored");
    rig.manager->CopyDemoSaveToRamRomSave(kRamRomFolderNum, demo);
    Check(rig.manager->GetSaveForFoldernum(kRamRomFolderNum)->options == 0x1234,
          "the demo folder takes the recording's save");
    rig.manager->WriteSave(*rig.manager->GetSaveForFoldernum(kRamRomFolderNum));
    Check(rig.storage->WriteCount() == writes, "and is never written to the chip");
    rig.manager->ResetRamRomSave();
    Check(*rig.manager->GetSaveForFoldernum(kRamRomFolderNum) == BlankSave(), "resetting it blanks it");
    Check(rig.storage->WriteCount() == writes, "also in memory only");
}

void TestStageTimesThroughTheManager() {
    Rig rig;
    rig.manager->ValidateSaves();
    SaveData save = *rig.manager->GetSaveForFoldernum(0);

    Check(!rig.manager->GetStageCompletedForDifficulty(save, 3, kDifficultyAgent), "nothing is completed at first");
    rig.manager->CheckStageDifficultyTime(save, 3, kDifficultyAgent, 400);
    Check(rig.manager->GetStageDifficultyTime(save, 3, kDifficultyAgent) == 400, "a first time is stored");
    Check(rig.manager->GetStageCompletedForDifficulty(save, 3, kDifficultyAgent), "and completes the level");
    rig.manager->CheckStageDifficultyTime(save, 3, kDifficultyAgent, 500);
    Check(rig.manager->GetStageDifficultyTime(save, 3, kDifficultyAgent) == 400, "a slower time is ignored");
    rig.manager->CheckStageDifficultyTime(save, 3, kDifficultyAgent, 400);
    Check(rig.manager->GetStageDifficultyTime(save, 3, kDifficultyAgent) == 400, "so is an equal one");
    rig.manager->CheckStageDifficultyTime(save, 3, kDifficultyAgent, 399);
    Check(rig.manager->GetStageDifficultyTime(save, 3, kDifficultyAgent) == 399, "a faster one replaces it");
    Check(!rig.manager->GetStageCompletedForDifficulty(save, 3, kDifficultySecret), "other difficulties are separate");

    rig.manager->CheckStageDifficultyTime(save, 5, kDifficulty00, 99999999);
    Check(rig.manager->GetStageDifficultyTime(save, 5, kDifficulty00) == 0x3FF,
          "the placeholder time the decomp passes for skipped difficulties saturates");
    rig.manager->CheckStageDifficultyTime(save, 5, kDifficulty00, 0);
    Check(rig.manager->GetStageDifficultyTime(save, 5, kDifficulty00) == 0x4F, "and a zero time is stored as 0x4F");

    SaveData before = save;
    rig.manager->CheckStageDifficultyTime(save, 20, kDifficultyAgent, 10);
    rig.manager->CheckStageDifficultyTime(save, 0, 4, 10);
    rig.manager->CheckStageDifficultyTime(save, -1, kDifficultyAgent, 10);
    Check(save == before, "out-of-range levels and difficulties are ignored");
    Check(!rig.manager->GetStageCompletedForDifficulty(save, 20, 0) &&
              !rig.manager->GetStageCompletedForDifficulty(save, 0, 4),
          "and never complete");
}

void TestSevenModeTimeFollowsTheStoredFolder() {
    Rig rig;
    rig.manager->ValidateSaves();
    SaveData copy = rig.manager->CopySave(0);

    Check(rig.manager->GetStageDifficultyTime(copy, 0, kDifficulty007) == 0, "007 mode has no time while locked");
    Check(!rig.manager->GetStageCompletedForDifficulty(copy, 0, kDifficulty007), "and is not completed");

    // The unlock is looked up in the folder's stored save, not in the copy
    // that is being asked.
    copy.flag007 = 1;
    Check(rig.manager->GetStageDifficultyTime(copy, 0, kDifficulty007) == 0,
          "an edited copy doesn't unlock it by itself");

    SaveData stored = *rig.manager->GetSaveForFoldernum(0);
    stored.flag007 = 1;
    rig.manager->OverwriteSaveSlotWithNewSave(rig.manager->GetSaveForFoldernum(0), stored);
    Check(rig.manager->Is007ModeUnlocked(0), "once the folder itself has it");
    Check(rig.manager->GetStageDifficultyTime(copy, 7, kDifficulty007) == 0x3FF,
          "every level reads the maximum on 007");
    Check(rig.manager->GetStageCompletedForDifficulty(copy, 7, kDifficulty007), "and counts as completed");
    Check(!rig.manager->Is007ModeUnlocked(1) && !rig.manager->Is007ModeUnlocked(9),
          "other folders and unknown ones stay locked");

    SaveData fresh = *rig.manager->GetSaveForFoldernum(0);
    rig.manager->CheckStageDifficultyTime(fresh, 2, kDifficulty007, 100);
    Check(fresh == *rig.manager->GetSaveForFoldernum(0), "007 times are never stored");
}

void TestBonds() {
    Rig rig;
    rig.manager->ValidateSaves();
    for (int32_t folder = 0; folder < kFolderCount; folder++) {
        Check(rig.manager->GetBondForFolder(static_cast<uint32_t>(folder)) == kBondBrosnan,
              "retail has Brosnan in every folder");
        Check(GetSelectedBond(*rig.manager->GetSaveForFoldernum(static_cast<uint32_t>(folder))) ==
                  static_cast<uint32_t>(folder),
              "a new folder's bond field is its folder number, as the decomp writes it");
        Check(rig.manager->SelectedBond(folder) == folder, "which validation loads as the selected bond");
    }
    rig.manager->SetSelectedBondToFolder(2, kBondConnery);
    Check(rig.manager->SelectedBond(2) == kBondBrosnan, "choosing another Bond is forced back to Brosnan");
    rig.manager->SetSelectedBondToFolder(7, kBondConnery);
    Check(rig.manager->GetBondForFolder(7) == kBondBrosnan, "an unknown folder is Brosnan");
}

} // namespace

int main() {
    TestAMissingSaveBecomesFourBlankFolders();
    TestAnAllZeroChipRecovers();
    TestAnErasedChipRecovers();
    TestAValidChipIsLeftAlone();
    TestChangesSurviveARestart();
    TestEachChangeMovesTheFolderToTheSpareSlot();
    TestUnlockingACheatTwiceWritesOnce();
    TestCheatUnlockIgnoresBadArguments();
    TestACorruptSlotResetsOnlyItsFolder();
    TestEveryByteOfASlotIsProtected();
    TestACorruptHeaderIsRewrittenAndKeepsTheSaves();
    TestAnInterruptedUpdateKeepsTheNewerCopy();
    TestStorageThatCannotReadFallsBack();
    TestStorageThatCannotWriteStillWorksInMemory();
    TestNoEepromMeansNoSaving();
    TestTheManagerStartsAsUninitializedSlots();
    TestFolderLookupAndValidity();
    TestCopySave();
    TestTheDemoFolderIsMemoryOnly();
    TestStageTimesThroughTheManager();
    TestSevenModeTimeFollowsTheStoredFolder();
    TestBonds();
    return gFailures == 0 ? 0 : 1;
}
