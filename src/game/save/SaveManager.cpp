#include "game/save/SaveManager.h"

#include <algorithm>
#include <utility>

namespace Frigate::Game::Save {

namespace {

// smallSave: two checksums and 24 bytes of data in front of the folders.
constexpr size_t kHeaderSize = 32;
constexpr size_t kHeaderDataOffset = 8;
constexpr uint8_t kHeaderBlock = 0;
// The folders start after the header, one 96-byte slot after another.
constexpr uint8_t kSavesBlock = 4;
constexpr uint8_t kSaveBlocks = kSaveDataSize / kEepromBlockSize;

constexpr uint8_t kHeaderMarker = SaveFlagsSet(kFolder3, 0, kBondConnery, false);

int32_t ReadBe32(const uint8_t* in) {
    return static_cast<int32_t>((static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
                                (static_cast<uint32_t>(in[2]) << 8) | static_cast<uint32_t>(in[3]));
}

void WriteBe32(uint8_t* out, int32_t value) {
    const auto bits = static_cast<uint32_t>(value);
    out[0] = static_cast<uint8_t>(bits >> 24);
    out[1] = static_cast<uint8_t>(bits >> 16);
    out[2] = static_cast<uint8_t>(bits >> 8);
    out[3] = static_cast<uint8_t>(bits);
}

} // namespace

SaveManager::SaveManager(std::shared_ptr<SaveStorage> storage) : storage_(std::move(storage)) {
    selectedBond_.fill(kBondBrosnan);
}

int32_t SaveManager::SlotIndex(const SaveData* save) const {
    const SaveData* first = saves_.data();
    if (save < first || save >= first + kEepromSaveSlots) {
        return -1;
    }
    return static_cast<int32_t>(save - first);
}

void SaveManager::WriteSmallSave(const std::array<uint8_t, kHeaderSize>& header) {
    if (!storage_->Present()) {
        return;
    }
    std::array<uint8_t, kHeaderSize> bytes = header;
    const Checksums sums = GenerateChecksums(bytes.data() + kHeaderDataOffset, bytes.data() + kHeaderSize);
    WriteBe32(&bytes[0], sums.chksum1);
    WriteBe32(&bytes[4], sums.chksum2);
    storage_->Write(kHeaderBlock, bytes.data(), bytes.size());
}

void SaveManager::WriteSave(SaveData& save) {
    const int32_t index = SlotIndex(&save);
    if (index < 0 || !storage_->Present()) {
        return;
    }
    const Checksums sums = SaveChecksums(save);
    save.chksum1 = sums.chksum1;
    save.chksum2 = sums.chksum2;
    const SaveBytes bytes = SerializeSave(save);
    storage_->Write(static_cast<uint8_t>(kSavesBlock + index * kSaveBlocks), bytes.data(), bytes.size());
}

void SaveManager::ResetSave(SaveData& save) {
    save = BlankSave();
    WriteSave(save);
}

void SaveManager::ResetRamRomSave() {
    saves_[kSaveSlotRamRom] = BlankSave();
}

bool SaveManager::IsFolderValid(int32_t folder) {
    return (folder >= 0 && folder < kFolderCount) || folder == kRamRomFolderNum;
}

SaveData* SaveManager::GetSaveForFoldernum(uint32_t folder) {
    return const_cast<SaveData*>(std::as_const(*this).GetSaveForFoldernum(folder));
}

const SaveData* SaveManager::GetSaveForFoldernum(uint32_t folder) const {
    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        if (!GetSaveFlagDoReset(saves_[i]) && GetSaveFolder(saves_[i]) == folder) {
            return &saves_[i];
        }
    }
    if (folder == static_cast<uint32_t>(kRamRomFolderNum)) {
        return &saves_[kSaveSlotRamRom];
    }
    return nullptr;
}

SaveData SaveManager::CopySave(int32_t folder) const {
    const SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    return save ? *save : BlankSave();
}

int32_t SaveManager::GetSaveFlagDoResetAnyFolder() const {
    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        if (GetSaveFlagDoReset(saves_[i])) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void SaveManager::BuildWriteNewSave(uint32_t folder) {
    const int32_t free = GetSaveFlagDoResetAnyFolder();
    if (free >= 0) {
        SaveData& slot = saves_[static_cast<size_t>(free)];
        slot = BlankSave();
        SetSaveFolder(slot, folder);
        SetSaveFlagDoReset(slot, false);
        SetSelectedBond(slot, static_cast<int32_t>(folder));
        WriteSave(slot);
    }
}

void SaveManager::ValidateSaves() {
    if (!storage_->Present()) {
        return;
    }

    // A failed read leaves the buffers as they were: zeros before the first
    // load.
    std::array<uint8_t, kHeaderSize> header{};
    storage_->Read(kHeaderBlock, header.data(), header.size());

    bool headerOk = true;
    // A customised header isn't assumed to be sound.
    if (header[kHeaderDataOffset] != kHeaderMarker) {
        headerOk = false;
    }
    const Checksums headerSums = GenerateChecksums(header.data() + kHeaderDataOffset, header.data() + kHeaderSize);
    if (headerSums.chksum1 != ReadBe32(&header[0]) || headerSums.chksum2 != ReadBe32(&header[4])) {
        headerOk = false;
    }
    if (!headerOk) {
        header.fill(0);
        header[kHeaderDataOffset] = kHeaderMarker;
        WriteSmallSave(header);
    }

    std::array<uint8_t, kSaveDataSize * kEepromSaveSlots> raw{};
    if (storage_->Read(kSavesBlock, raw.data(), raw.size())) {
        for (size_t i = 0; i < kEepromSaveSlots; i++) {
            SaveBytes bytes;
            std::copy_n(raw.begin() + i * kSaveDataSize, kSaveDataSize, bytes.begin());
            saves_[i] = DeserializeSave(bytes);
        }
    }

    for (size_t i = 0; i < kEepromSaveSlots; i++) {
        const Checksums sums = SaveChecksums(saves_[i]);
        if (sums.chksum1 != saves_[i].chksum1 || sums.chksum2 != saves_[i].chksum2) {
            ResetSave(saves_[i]);
        }
    }

    for (int32_t folder = 0; folder < kFolderCount; folder++) {
        int32_t live = -1;
        int32_t slot = -1;

        for (int32_t j = 0; j < static_cast<int32_t>(kEepromSaveSlots); j++) {
            SaveData& candidate = saves_[static_cast<size_t>(j)];
            if (!GetSaveFlagDoReset(candidate) && GetSaveFolder(candidate) == static_cast<uint32_t>(folder)) {
                if (live < 0) {
                    live = j;
                    slot = static_cast<int32_t>(GetSaveFlagSlot(candidate));
                } else {
                    // Two copies of a folder: an interrupted rewrite. The one
                    // whose counter is a step ahead is the newer.
                    const int32_t slot2 = static_cast<int32_t>(GetSaveFlagSlot(candidate));
                    if (slot2 == (slot + 1) % 4) {
                        ResetSave(saves_[static_cast<size_t>(live)]);
                        live = j;
                        slot = slot2;
                    } else {
                        ResetSave(candidate);
                    }
                }
            }
        }

        if (live < 0) {
            BuildWriteNewSave(static_cast<uint32_t>(folder));
        }
    }

    for (int32_t folder = 0; folder < kFolderCount; folder++) {
        const SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
        if (save) {
            selectedBond_[static_cast<size_t>(folder)] = static_cast<int32_t>(GetSelectedBond(*save));
        }
    }
}

void SaveManager::OverwriteSaveSlotWithNewSave(SaveData* save1, const SaveData& save2) {
    const int32_t free = GetSaveFlagDoResetAnyFolder();
    if (free < 0) {
        return;
    }

    // save2 may be a copy of the very slot being claimed or freed.
    const SaveData incoming = save2;
    SaveData& target = saves_[static_cast<size_t>(free)];
    target = incoming;

    int32_t slot = 0;
    if (save1) {
        slot = static_cast<int32_t>(GetSaveFlagSlot(*save1) + 1) % 4;
    }

    SetSaveFlagDoReset(target, false);
    SetSaveFlagSlot(target, slot);
    WriteSave(target);

    if (save1) {
        ResetSave(*save1);
    }
}

void SaveManager::CopyDemoSaveToRamRomSave(uint32_t folder, const SaveData& save) {
    if (folder == static_cast<uint32_t>(kRamRomFolderNum)) {
        saves_[kSaveSlotRamRom] = save;
    }
}

uint32_t SaveManager::GetStageDifficultyTime(const SaveData& save, int32_t level, int32_t difficulty) const {
    if (level >= kLevelDam && level < kSoloLevelCount && difficulty >= kDifficultyAgent &&
        difficulty < kDifficultyCount) {
        if (difficulty == kDifficulty007) {
            return Is007ModeUnlocked(GetSaveFolder(save)) ? 0x3FF : 0;
        }
        return GetPackedStageTime(save, level, difficulty);
    }
    return 0;
}

bool SaveManager::GetStageCompletedForDifficulty(const SaveData& save, int32_t level, int32_t difficulty) const {
    if (level >= kLevelDam && level < kSoloLevelCount && difficulty >= kDifficultyAgent &&
        difficulty <= kDifficulty007) {
        return GetStageDifficultyTime(save, level, difficulty) != 0;
    }
    return false;
}

void SaveManager::CheckStageDifficultyTime(SaveData& save, int32_t level, int32_t difficulty, int32_t newtime) const {
    if (level >= kLevelDam && level < kSoloLevelCount && difficulty >= kDifficultyAgent &&
        difficulty <= kDifficulty007) {
        const int32_t time = static_cast<int32_t>(GetStageDifficultyTime(save, level, difficulty));
        if (time == 0 || newtime < time) {
            SetPackedStageTime(save, level, difficulty, newtime);
        }
    }
}

bool SaveManager::Is007ModeUnlocked(uint32_t folder) const {
    const SaveData* save = GetSaveForFoldernum(folder);
    return save != nullptr && Frigate::Game::Save::Is007ModeUnlocked(*save);
}

void SaveManager::SaveFolderUnlockCheat(int32_t folder, int32_t cheat) {
    if (folder < 0 || folder >= kFolderCount || cheat < 0 || cheat >= kCheatCount) {
        return;
    }

    SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    if (save && IsCheatUnlocked(*save, cheat)) {
        return;
    }

    SaveData newSave = BlankSave();
    if (save) {
        newSave = *save;
    } else {
        SetSaveFolder(newSave, static_cast<uint32_t>(folder));
    }
    SetCheatUnlocked(newSave, cheat);
    OverwriteSaveSlotWithNewSave(save, newSave);
}

bool SaveManager::IsCheatUnlockedForFolder(int32_t folder, int32_t cheat) const {
    if (folder < 0) {
        return false;
    }
    const SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    return save != nullptr && IsCheatUnlocked(*save, cheat);
}

StageStatus SaveManager::IsStageUnlockedAtDifficulty(int32_t folder, int32_t level, int32_t difficulty) const {
    if (IsFolderValid(folder) && level >= kLevelDam && level < kSoloLevelCount && difficulty >= kDifficultyAgent &&
        difficulty < kDifficultyCount) {
        const SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));

        if (save) {
            if (GetStageCompletedForDifficulty(*save, level, difficulty)) {
                return StageStatus::Completed;
            }

            // The bonus levels cannot be completed below their own difficulty.
            if ((level == kLevelAztec && difficulty < kDifficultySecret) ||
                (level == kLevelEgypt && difficulty < kDifficulty00)) {
                return StageStatus::Locked;
            }

            // Every earlier level done on this or a harder difficulty unlocks
            // it, which is also how a cheat unlocks levels without playing them.
            for (int32_t i = difficulty; i < kDifficultyCount; i++) {
                int32_t stage;
                for (stage = kLevelDam; stage < level; stage++) {
                    if (!GetStageCompletedForDifficulty(*save, stage, i)) {
                        break;
                    }
                }
                if (level <= stage) {
                    return StageStatus::Unlocked;
                }
            }

            if (difficulty < kDifficulty007 && level < kLevelAztec) {
                for (int32_t i = difficulty; i < kDifficultyCount; i++) {
                    if (GetStageCompletedForDifficulty(*save, level - 1, i)) {
                        return StageStatus::Unlocked;
                    }
                }
            }

            if (difficulty < kDifficulty007) {
                int32_t i;
                for (i = kLevelDam; i < kLevelAztec; i++) {
                    if (!GetStageCompletedForDifficulty(*save, i, kDifficultyAgent)) {
                        break;
                    }
                }
                if (i >= kLevelAztec) {
                    for (i = kDifficultyAgent; i < difficulty; i++) {
                        if (!GetStageCompletedForDifficulty(*save, level, i)) {
                            break;
                        }
                    }
                    if (difficulty <= i) {
                        return StageStatus::Unlocked;
                    }
                }
            }
        }

        if (level == kLevelDam) {
            return StageStatus::Unlocked;
        }
        if (debugEnableAgentLevels_ && difficulty == kDifficultyAgent) {
            return StageStatus::Unlocked;
        }
        if (debugEnableAllLevels_) {
            return StageStatus::Unlocked;
        }
    }
    return StageStatus::Locked;
}

void SaveManager::UnlockStageInFolderAtDifficulty(int32_t folder, int32_t level, int32_t difficulty, int32_t newtime) {
    if (folder < 0 || folder >= kFolderCount || level < kLevelDam || level >= kSoloLevelCount ||
        difficulty < kDifficultyAgent || difficulty >= kDifficultyCount) {
        return;
    }

    SaveData newSave = BlankSave();
    SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    if (save) {
        newSave = *save;
    } else {
        SetSaveFolder(newSave, static_cast<uint32_t>(folder));
    }

    for (int32_t i = difficulty; i >= kDifficultyAgent; i--) {
        CheckStageDifficultyTime(newSave, level, i, i == difficulty ? newtime : 99999999);
    }

    OverwriteSaveSlotWithNewSave(save, newSave);
}

StageDifficulty SaveManager::GetHighestStageDifficultyCompletedForFolder(int32_t folder) const {
    const SaveData* save = folder < 0 ? nullptr : GetSaveForFoldernum(static_cast<uint32_t>(folder));

    if (save) {
        for (int32_t difficulty = kDifficulty007; difficulty >= kDifficultyAgent; difficulty--) {
            for (int32_t level = kLevelEgypt; level >= kLevelDam; level--) {
                if (GetStageCompletedForDifficulty(*save, level, difficulty)) {
                    return { level, difficulty };
                }
            }
        }
    }
    return { kLevelDam - 1, kDifficultyMulti };
}

int32_t SaveManager::GetHighestStageUnlockedForFolder(int32_t folder) const {
    if (folder >= 0 && GetSaveForFoldernum(static_cast<uint32_t>(folder)) != nullptr) {
        for (int32_t level = kLevelEgypt; level >= kLevelDam; level--) {
            for (int32_t difficulty = kDifficultyAgent; difficulty < kDifficultyCount; difficulty++) {
                if (IsStageUnlockedAtDifficulty(folder, level, difficulty) != StageStatus::Locked) {
                    return level;
                }
            }
        }
    }
    return kLevelDam;
}

int32_t SaveManager::GetHighestStageUnlockedAnyFolder() const {
    int32_t highest = kLevelDam;
    for (int32_t folder = kFolder1; folder < kFolderCount; folder++) {
        const int32_t found = GetHighestStageUnlockedForFolder(folder);
        if (highest < found) {
            highest = found;
        }
    }
    return highest;
}

bool SaveManager::IsCradleCompletedForFolder(int32_t folder) const {
    return IsStageUnlockedAtDifficulty(folder, kLevelCradle, kDifficultyAgent) == StageStatus::Completed ||
           IsStageUnlockedAtDifficulty(folder, kLevelCradle, kDifficultySecret) == StageStatus::Completed ||
           IsStageUnlockedAtDifficulty(folder, kLevelCradle, kDifficulty00) == StageStatus::Completed;
}

bool SaveManager::IsAztecCompletedOnSecretOr00ForFolder(int32_t folder) const {
    return IsStageUnlockedAtDifficulty(folder, kLevelAztec, kDifficultySecret) == StageStatus::Completed ||
           IsStageUnlockedAtDifficulty(folder, kLevelAztec, kDifficulty00) == StageStatus::Completed;
}

bool SaveManager::IsEgyptCompletedOn00ForFolder(int32_t folder) const {
    return IsStageUnlockedAtDifficulty(folder, kLevelEgypt, kDifficulty00) == StageStatus::Completed;
}

bool SaveManager::IsCradleCompletedAnyFolder() const {
    for (int32_t folder = kFolder1; folder < kFolderCount; folder++) {
        if (IsCradleCompletedForFolder(folder)) {
            return true;
        }
    }
    return false;
}

bool SaveManager::IsAztecCompletedOnSecretOr00AnyFolder() const {
    for (int32_t folder = kFolder1; folder < kFolderCount; folder++) {
        if (IsAztecCompletedOnSecretOr00ForFolder(folder)) {
            return true;
        }
    }
    return false;
}

bool SaveManager::IsEgyptCompletedOn00AnyFolder() const {
    for (int32_t folder = kFolder1; folder < kFolderCount; folder++) {
        if (IsEgyptCompletedOn00ForFolder(folder)) {
            return true;
        }
    }
    return false;
}

void SaveManager::DeleteSaveForFolder(int32_t folder) {
    if (folder < kFolder1 || folder >= kFolderCount) {
        return;
    }
    SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    if (!save) {
        return;
    }
    const StageDifficulty highest = GetHighestStageDifficultyCompletedForFolder(folder);
    if (highest.level >= kLevelDam && highest.difficulty >= kDifficultyAgent) {
        *save = BlankSave();
        SetSaveFolder(*save, static_cast<uint32_t>(folder));
        SetSaveFlagDoReset(*save, false);
        SetSelectedBond(*save, folder);
        SetSelectedBondToFolder(folder, folder);
        WriteSave(*save);
    }
}

void SaveManager::InitializeAllTimes(uint32_t folder) {
    SaveData* save = GetSaveForFoldernum(folder);
    if (!save) {
        // The decomp writes through the null pointer; validated saves give
        // every folder a slot, so only a bad folder number gets here.
        return;
    }
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        for (int32_t difficulty = kDifficultyAgent; difficulty < kDifficulty007; difficulty++) {
            CheckStageDifficultyTime(*save, level, difficulty, 99999999);
        }
    }
}

void SaveManager::CopyFolderToFirstFree(int32_t folder) {
    if (folder < kFolder1 || folder >= kFolderCount) {
        return;
    }
    const SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    if (!save) {
        return;
    }
    StageDifficulty highest = GetHighestStageDifficultyCompletedForFolder(folder);
    if (highest.level < kLevelDam || highest.difficulty < kDifficultyAgent) {
        return;
    }

    int32_t other;
    for (other = kFolder1; other != kFolderCount; other++) {
        if (GetSaveForFoldernum(static_cast<uint32_t>(other)) == nullptr) {
            break;
        }
        highest = GetHighestStageDifficultyCompletedForFolder(other);
        if (highest.level < kLevelDam && highest.difficulty < kDifficultyAgent) {
            break;
        }
    }

    if (other < kFolderCount) {
        SaveData* target = GetSaveForFoldernum(static_cast<uint32_t>(other));
        SaveData newSave = *save;
        SetSaveFolder(newSave, static_cast<uint32_t>(other));
        SetSelectedBondToFolder(other, GetBondForFolder(static_cast<uint32_t>(folder)));
        OverwriteSaveSlotWithNewSave(target, newSave);
    }
}

void SaveManager::UpdateSelectedBondInSave(int32_t folder) {
    if (folder < kFolder1 || folder >= kFolderCount) {
        return;
    }
    SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));
    SaveData newSave = BlankSave();
    if (save) {
        newSave = *save;
    } else {
        SetSaveFolder(newSave, static_cast<uint32_t>(folder));
    }

    const int32_t bond = selectedBond_[static_cast<size_t>(folder)];
    if (bond != static_cast<int32_t>(GetSelectedBond(newSave))) {
        SetSelectedBond(newSave, bond);
        OverwriteSaveSlotWithNewSave(save, newSave);
    }
}

void SaveManager::ClearSavefileForFolder(int32_t folder, const SettingsCapture& captureSettings) {
    if (folder < kFolder1 || folder >= kFolderCount) {
        return;
    }
    SaveData* save = GetSaveForFoldernum(static_cast<uint32_t>(folder));

    SaveData saveToCopy = BlankSave();
    if (save) {
        saveToCopy = *save;
    } else {
        SetSaveFolder(saveToCopy, static_cast<uint32_t>(folder));
    }

    SaveData newSave = saveToCopy;
    if (captureSettings) {
        captureSettings(newSave);
    }

    if (!(newSave == saveToCopy)) {
        OverwriteSaveSlotWithNewSave(save, newSave);
    }
}

uint8_t SaveManager::GetBondForFolder(uint32_t folder) const {
    if constexpr (kAllBonds) {
        if (folder < static_cast<uint32_t>(kFolderCount)) {
            return static_cast<uint8_t>(selectedBond_[folder]);
        }
    }
    return kBondBrosnan;
}

void SaveManager::SetSelectedBondToFolder(int32_t folder, int32_t bond) {
    if (folder < 0 || folder >= kFolderCount) {
        return;
    }
    selectedBond_[static_cast<size_t>(folder)] = kAllBonds ? bond : kBondBrosnan;
}

} // namespace Frigate::Game::Save
