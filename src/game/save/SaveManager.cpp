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

// ALL_BONDS is a decomp build option for letting the player pick a Bond;
// retail has Brosnan only.
constexpr bool kAllBonds = false;

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
