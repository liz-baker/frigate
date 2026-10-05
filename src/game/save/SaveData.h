#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The GoldenEye decomp's save_data (file.h) and the accessors in file2.c that
// read and write its packed fields. No I/O here; SaveManager.h owns the slots
// and the storage. On-disk layout: docs/formats/goldeneye-eeprom-save.md.
namespace Frigate::Game::Save {

// LEVEL_SOLO_SEQUENCE.
constexpr int32_t kLevelDam = 0;
constexpr int32_t kLevelFacility = 1;
constexpr int32_t kLevelCradle = 17;
constexpr int32_t kLevelAztec = 18;
constexpr int32_t kLevelEgypt = 19;
constexpr int32_t kSoloLevelCount = 20;

// DIFFICULTY.
constexpr int32_t kDifficultyAgent = 0;
constexpr int32_t kDifficultySecret = 1;
constexpr int32_t kDifficulty00 = 2;
constexpr int32_t kDifficulty007 = 3;
constexpr int32_t kDifficultyCount = 4;
constexpr int32_t kDifficultyMulti = -1;

// ALL_BONDS is a decomp build option for letting the player pick a Bond;
// retail has Brosnan only.
constexpr bool kAllBonds = false;

// BOND.
constexpr int32_t kBondBrosnan = 0;
constexpr int32_t kBondConnery = 1;

// FOLDER1..FOLDER4 and MAX_FOLDER_COUNT, RAMROM_FOLDERNUM.
constexpr int32_t kFolder1 = 0;
constexpr int32_t kFolderCount = 4;
constexpr int32_t kFolder3 = 2;
constexpr int32_t kRamRomFolderNum = 0x64;

// CHEAT_INPUT_BUFFER_SIZE.
constexpr int32_t kCheatCount = 20;

// OPTION_* masks of SaveData::options.
constexpr uint16_t kOptionInvertLook = 0x0001;
constexpr uint16_t kOptionAutoAim = 0x0002;
constexpr uint16_t kOptionAimControl = 0x0004;
constexpr uint16_t kOptionSightOnScreen = 0x0008;
constexpr uint16_t kOptionLookAhead = 0x0010;
constexpr uint16_t kOptionDisplayAmmo = 0x0020;
constexpr uint16_t kOptionScreenWide = 0x0040;
constexpr uint16_t kOptionScreenRatio = 0x0080;
constexpr uint16_t kOptionControlType = 0x0700;
constexpr uint16_t kOptionScreenCinema = 0x0800;
constexpr uint16_t kDefaultOptions = kOptionAutoAim | kOptionSightOnScreen | kOptionLookAhead | kOptionDisplayAmmo;

// SAVEFLAG_* masks of SaveData::completionBitflags.
constexpr uint8_t kSaveFlagFolder = 0x07;
constexpr uint8_t kSaveFlagSlot = 0x18;
constexpr uint8_t kSaveFlagBond = 0x60;
constexpr uint8_t kSaveFlagDoReset = 0x80;

// SAVEFLAGS_SET. Its field positions differ from the SAVEFLAG_* masks the
// accessors use (the macro is only ever used for a marker byte and the blank
// save's reset flag), so it is kept as the decomp spells it.
constexpr uint8_t SaveFlagsSet(int32_t folder, int32_t slot, int32_t bond, bool reset) {
    return static_cast<uint8_t>(((folder << 5) & 0xE0) | ((slot * 8) & 0x18) | ((bond << 1) & 0x6) |
                                (reset ? kSaveFlagDoReset : 0));
}

constexpr size_t kStageTimeBytes = (kSoloLevelCount - 1) * 4;
constexpr size_t kSaveDataSize = 96;
// The completion flags byte onward is what the checksums cover.
constexpr size_t kChecksumOffset = 8;

// save_data. All-zero is the state of the decomp's uninitialized slots; see
// BlankSave() for BLANKSAVEDATA.
struct SaveData {
    int32_t chksum1 = 0;
    int32_t chksum2 = 0;
    uint8_t completionBitflags = 0;
    uint8_t flag007 = 0;
    uint8_t musicVol = 0;
    uint8_t sfxVol = 0;
    uint16_t options = 0;
    std::array<uint8_t, 3> unlockedCheats{};
    uint8_t padding = 0;
    std::array<uint8_t, kStageTimeBytes> times{};
    // The struct's two trailing alignment bytes. They are covered by the
    // checksum and copied with the struct, so they are kept as read.
    std::array<uint8_t, 2> tail{};

    bool operator==(const SaveData&) const = default;
};

using SaveBytes = std::array<uint8_t, kSaveDataSize>;

// BLANKSAVEDATA: the do-reset flag set, volumes at 0xFF, default options.
SaveData BlankSave();

// Big-endian, as the console wrote it.
SaveBytes SerializeSave(const SaveData& save);
SaveData DeserializeSave(const SaveBytes& bytes);

// fileGenerateCRC over bytes [begin, end) of a buffer.
struct Checksums {
    int32_t chksum1;
    int32_t chksum2;
};
Checksums GenerateChecksums(const uint8_t* begin, const uint8_t* end);

// fileWriteSave's checksum: over the completion flags to the end of the slot.
Checksums SaveChecksums(const SaveData& save);

// fileGetSaveFolder / fileSetSaveFoldernum.
uint32_t GetSaveFolder(const SaveData& save);
void SetSaveFolder(SaveData& save, uint32_t folder);
// fileGetSaveFlagSlot / fileResetSaveFlagSlot: the wear-levelling slot counter.
uint32_t GetSaveFlagSlot(const SaveData& save);
void SetSaveFlagSlot(SaveData& save, int32_t slot);
// fileGetSelectedBond / fileSetSelectedBond.
uint32_t GetSelectedBond(const SaveData& save);
void SetSelectedBond(SaveData& save, int32_t bond);
// fileGetSaveFlagDoReset / fileSetSaveFlagDoReset: set on a free slot.
bool GetSaveFlagDoReset(const SaveData& save);
void SetSaveFlagDoReset(SaveData& save, bool enable);

// The stored best time of a level on difficulty Agent, Secret Agent or 00
// Agent, in seconds (10 bits, packed; 0 is not completed). 007 mode keeps no
// time of its own, so it reads 0 here; SaveManager::GetStageDifficultyTime
// has the 007 rule. Out-of-range arguments read 0.
uint32_t GetPackedStageTime(const SaveData& save, int32_t level, int32_t difficulty);
// fileSetDifficultyStageTime: stores `newtime`, 0 becoming 0x4F and anything
// over 0x3FF becoming 0x3FF. Ignored for 007 and out-of-range arguments.
void SetPackedStageTime(SaveData& save, int32_t level, int32_t difficulty, int32_t newtime);

// Marks a level as not completed on a difficulty. The game never takes a
// completion back, so this has no decomp counterpart; it exists for the
// save editor. Ignored for 007 and out-of-range arguments.
void ClearPackedStageTime(SaveData& save, int32_t level, int32_t difficulty);

// fileGetIsCheatUnlocked / fileSetSaveCheatUnlocked.
bool IsCheatUnlocked(const SaveData& save, int32_t cheat);
void SetCheatUnlocked(SaveData& save, int32_t cheat);
// Likewise without a decomp counterpart: locks a cheat again.
void ClearCheatUnlocked(SaveData& save, int32_t cheat);

// The 007 half of fileIs007ModeUnlocked once the folder's slot is found: the
// 007 flag, or every level completed on 00 Agent.
bool Is007ModeUnlocked(const SaveData& save);

} // namespace Frigate::Game::Save
