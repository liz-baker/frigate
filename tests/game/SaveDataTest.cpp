#include "game/save/SaveData.h"

#include <cstdio>

using namespace Frigate::Game::Save;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void TestTheBlankSave() {
    const SaveData blank = BlankSave();
    Check(blank.completionBitflags == 0x80, "a blank save is flagged free to reuse, folder 0, slot 0, Brosnan");
    Check(blank.flag007 == 0, "007 mode starts locked");
    Check(blank.musicVol == 0xFF && blank.sfxVol == 0xFF, "volumes start at full");
    Check(blank.options == 0x3A, "default options are auto-aim, sight, look ahead and ammo display");
    Check(blank.unlockedCheats == std::array<uint8_t, 3>{}, "no cheats are unlocked");
    Check(GetSaveFlagDoReset(blank), "the free flag reads back");
}

void TestLayoutIs96BytesBigEndian() {
    SaveData save = BlankSave();
    save.chksum1 = static_cast<int32_t>(0x01020304);
    save.chksum2 = static_cast<int32_t>(0xA1B2C3D4);
    save.options = 0x0A0B;
    save.unlockedCheats = { 0x11, 0x22, 0x33 };
    save.padding = 0x44;
    save.times.front() = 0x55;
    save.times.back() = 0x66;
    save.tail = { 0x77, 0x88 };

    const SaveBytes bytes = SerializeSave(save);
    Check(bytes.size() == 96, "a save is 96 bytes");
    Check(bytes[0] == 0x01 && bytes[3] == 0x04 && bytes[4] == 0xA1 && bytes[7] == 0xD4, "checksums are big-endian");
    Check(bytes[8] == 0x80 && bytes[9] == 0 && bytes[10] == 0xFF && bytes[11] == 0xFF, "flags and volumes follow");
    Check(bytes[12] == 0x0A && bytes[13] == 0x0B, "options are big-endian");
    Check(bytes[14] == 0x11 && bytes[15] == 0x22 && bytes[16] == 0x33 && bytes[17] == 0x44, "cheat bytes then padding");
    Check(bytes[18] == 0x55 && bytes[93] == 0x66, "76 bytes of times");
    Check(bytes[94] == 0x77 && bytes[95] == 0x88, "the two alignment bytes close the slot");
    Check(DeserializeSave(bytes) == save, "a save reads back as written");
}

void TestChecksumsMatchReferenceValues() {
    // Reference values from an independent evaluation of fileGenerateCRC
    // (with the generator decoded from randomGetNextFrom's instructions).
    const Checksums blank = SaveChecksums(BlankSave());
    Check(static_cast<uint32_t>(blank.chksum1) == 0xDBA8AE51 && static_cast<uint32_t>(blank.chksum2) == 0x8B55D250,
          "the blank save's checksums");

    uint8_t pattern[88];
    for (int i = 0; i < 88; i++) {
        pattern[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    const Checksums sums = GenerateChecksums(pattern, pattern + 88);
    Check(static_cast<uint32_t>(sums.chksum1) == 0xEE5C3E13 && static_cast<uint32_t>(sums.chksum2) == 0xCA97752F,
          "an arbitrary 88 bytes");

    uint8_t header[24] = { 0x42 };
    const Checksums headerSums = GenerateChecksums(header, header + 24);
    Check(static_cast<uint32_t>(headerSums.chksum1) == 0x89243833 &&
              static_cast<uint32_t>(headerSums.chksum2) == 0x87527848,
          "the 24-byte header");
}

void TestChecksumCoversEveryByteAfterTheChecksums() {
    SaveData save = BlankSave();
    const Checksums before = SaveChecksums(save);
    for (size_t i = 8; i < kSaveDataSize; i++) {
        SaveBytes bytes = SerializeSave(save);
        bytes[i] ^= 1;
        const Checksums after = SaveChecksums(DeserializeSave(bytes));
        Check(after.chksum1 != before.chksum1 || after.chksum2 != before.chksum2,
              "flipping any covered byte changes a checksum");
    }
    save.chksum1 = 1234;
    const Checksums stamped = SaveChecksums(save);
    Check(stamped.chksum1 == before.chksum1 && stamped.chksum2 == before.chksum2,
          "the stored checksums are not part of what is summed");
}

void TestFolderSlotBondAndResetFlags() {
    SaveData save = BlankSave();
    SetSaveFolder(save, 2);
    Check(GetSaveFolder(save) == 2 && GetSaveFlagDoReset(save), "the folder is the low three bits");
    SetSaveFolder(save, 0xFF);
    Check(GetSaveFolder(save) == 7 && GetSaveFlagSlot(save) == 0 && GetSelectedBond(save) == 0,
          "an oversized folder stays in its field");
    SetSaveFolder(save, 3);

    SetSaveFlagSlot(save, 2);
    Check(GetSaveFlagSlot(save) == 2 && (save.completionBitflags & 0x18) == 0x10, "the slot counter is bits 3 and 4");
    SetSaveFlagSlot(save, 5);
    Check(GetSaveFlagSlot(save) == 1, "the slot counter wraps at four");
    Check(GetSaveFolder(save) == 3, "and leaves the folder alone");

    SetSelectedBond(save, 2);
    Check(GetSelectedBond(save) == 2 && (save.completionBitflags & 0x60) == 0x40, "the bond is bits 5 and 6");
    SetSelectedBond(save, 3);
    Check(GetSelectedBond(save) == 3 && GetSaveFlagSlot(save) == 1 && GetSaveFolder(save) == 3,
          "bond 3 fills its field without spilling");

    SetSaveFlagDoReset(save, false);
    Check(!GetSaveFlagDoReset(save) && (save.completionBitflags & 0x80) == 0, "the free flag clears");
    Check(GetSaveFolder(save) == 3 && GetSelectedBond(save) == 3, "without touching the rest");
    SetSaveFlagDoReset(save, true);
    Check(GetSaveFlagDoReset(save), "and sets again");
}

void TestSaveFlagsSetMacro() {
    // The marker byte the header carries: folder 3 (value 2) in the top bits,
    // Connery in bits 1-2.
    Check(SaveFlagsSet(2, 0, 1, false) == 0x42, "SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE)");
    Check(SaveFlagsSet(0, 0, 0, true) == 0x80, "the blank save's flags");
    Check(SaveFlagsSet(3, 3, 3, true) == 0xFE, "every field full");
}

void TestStageTimesPackTenBitsPerEntry() {
    SaveData save = BlankSave();
    // Level 0 on Agent is bits 0-9: the first byte and the top two bits of the second.
    SetPackedStageTime(save, 0, kDifficultyAgent, 0x2A5);
    Check(save.times[0] == 0xA9 && save.times[1] == 0x40, "0x2A5 at offset 0 splits 8 + 2 bits");
    Check(GetPackedStageTime(save, 0, kDifficultyAgent) == 0x2A5, "and reads back");

    // Level 1 is bits 10-19: offset 2 into the second byte.
    SetPackedStageTime(save, 1, kDifficultyAgent, 0x3FF);
    Check(save.times[1] == 0x7F && save.times[2] == 0xF0, "0x3FF at offset 2 splits 6 + 4 bits");
    Check(GetPackedStageTime(save, 0, kDifficultyAgent) == 0x2A5, "its neighbour is untouched");

    // Level 2 is bits 20-29: offset 4; level 3 is bits 30-39: offset 6.
    SetPackedStageTime(save, 2, kDifficultyAgent, 0x155);
    SetPackedStageTime(save, 3, kDifficultyAgent, 0x0C3);
    Check(GetPackedStageTime(save, 2, kDifficultyAgent) == 0x155, "offset 4 reads back");
    Check(GetPackedStageTime(save, 3, kDifficultyAgent) == 0x0C3, "offset 6 reads back");
    Check(GetPackedStageTime(save, 1, kDifficultyAgent) == 0x3FF &&
              GetPackedStageTime(save, 0, kDifficultyAgent) == 0x2A5,
          "and none of the four disturbed another");
    Check(save.times[2] == 0xF5 && save.times[3] == 0x54 && save.times[4] == 0xC3,
          "offset 4 spans bytes 2-3 and offset 6 spans bytes 3-4");
}

void TestEveryEntryHoldsItsOwnValue() {
    SaveData save = BlankSave();
    for (int32_t difficulty = kDifficultyAgent; difficulty < kDifficulty007; difficulty++) {
        for (int32_t level = 0; level < kSoloLevelCount; level++) {
            SetPackedStageTime(save, level, difficulty, 100 + level * 17 + difficulty * 3);
        }
    }
    bool allMatch = true;
    for (int32_t difficulty = kDifficultyAgent; difficulty < kDifficulty007; difficulty++) {
        for (int32_t level = 0; level < kSoloLevelCount; level++) {
            allMatch &=
                GetPackedStageTime(save, level, difficulty) == static_cast<uint32_t>(100 + level * 17 + difficulty * 3);
        }
    }
    Check(allMatch, "all 60 stored times read back through each other's writes");
    Check(save.times[74] != 0 && save.times[75] == 0, "the 60 entries fill 75 bytes and the 76th is spare");

    // Overwriting with a smaller value must clear the bits the old one set.
    SetPackedStageTime(save, 7, kDifficultySecret, 0x3FF);
    SetPackedStageTime(save, 7, kDifficultySecret, 1);
    Check(GetPackedStageTime(save, 7, kDifficultySecret) == 1, "a lower time replaces a higher one cleanly");
    Check(GetPackedStageTime(save, 6, kDifficultySecret) == 100 + 6 * 17 + 3 &&
              GetPackedStageTime(save, 8, kDifficultySecret) == 100 + 8 * 17 + 3,
          "without bleeding into the neighbours");
}

void TestStageTimeLimitsAndRanges() {
    SaveData save = BlankSave();
    SetPackedStageTime(save, 4, kDifficulty00, 0);
    Check(GetPackedStageTime(save, 4, kDifficulty00) == 0x4F,
          "a stored zero becomes 0x4F so it still counts as completed");
    SetPackedStageTime(save, 4, kDifficulty00, 99999999);
    Check(GetPackedStageTime(save, 4, kDifficulty00) == 0x3FF, "times saturate at 0x3FF");

    SaveData fresh = BlankSave();
    SetPackedStageTime(fresh, kSoloLevelCount, kDifficultyAgent, 5);
    SetPackedStageTime(fresh, -1, kDifficultyAgent, 5);
    SetPackedStageTime(fresh, 0, kDifficulty007, 5);
    SetPackedStageTime(fresh, 0, -1, 5);
    Check(fresh == BlankSave(), "writes outside the levels, or to 007 mode, do nothing");
    Check(GetPackedStageTime(fresh, kSoloLevelCount, 0) == 0 && GetPackedStageTime(fresh, 0, kDifficulty007) == 0 &&
              GetPackedStageTime(fresh, -1, 0) == 0 && GetPackedStageTime(fresh, 0, -1) == 0,
          "and reads outside them give 0");
}

void TestCheatBits() {
    SaveData save = BlankSave();
    for (int32_t cheat = 0; cheat < kCheatCount; cheat++) {
        Check(!IsCheatUnlocked(save, cheat), "no cheat starts unlocked");
    }

    SetCheatUnlocked(save, 0);
    SetCheatUnlocked(save, 7);
    Check(save.unlockedCheats[0] == 0x81 && save.unlockedCheats[1] == 0 && save.unlockedCheats[2] == 0,
          "cheats 0-7 are the first byte");
    SetCheatUnlocked(save, 8);
    SetCheatUnlocked(save, 15);
    Check(save.unlockedCheats[1] == 0x81, "cheats 8-15 are the second byte");
    SetCheatUnlocked(save, 16);
    SetCheatUnlocked(save, 19);
    Check(save.unlockedCheats[2] == 0x09, "cheats 16-19 are the low bits of the third byte");

    for (int32_t cheat : { 0, 7, 8, 15, 16, 19 }) {
        Check(IsCheatUnlocked(save, cheat), "an unlocked cheat reads as unlocked");
    }
    for (int32_t cheat : { 1, 6, 9, 14, 17, 18 }) {
        Check(!IsCheatUnlocked(save, cheat), "and the others stay locked");
    }

    const SaveData before = save;
    SetCheatUnlocked(save, -1);
    SetCheatUnlocked(save, kCheatCount);
    Check(save == before, "an out-of-range cheat is ignored");
    Check(!IsCheatUnlocked(save, -1) && !IsCheatUnlocked(save, kCheatCount), "and never reads as unlocked");

    // Only the first 20 bits matter, but the high bits of byte three are real
    // storage and survive.
    save.unlockedCheats[2] = 0xF0;
    Check(!IsCheatUnlocked(save, 16) && !IsCheatUnlocked(save, 19), "bits 4-7 of the third byte unlock nothing");
}

void TestUnlockingEveryCheat() {
    SaveData save = BlankSave();
    for (int32_t cheat = 0; cheat < kCheatCount; cheat++) {
        SetCheatUnlocked(save, cheat);
    }
    Check(save.unlockedCheats == std::array<uint8_t, 3>{ 0xFF, 0xFF, 0x0F }, "all 20 cheats make 24 bits' worth");
}

void TestOptionMasks() {
    Check(kDefaultOptions == 0x003A, "DEFAULT_OPTIONS");
    Check((kOptionControlType >> 8) == 7, "the control style is three bits");
}

void TestSevenModeUnlock() {
    SaveData save = BlankSave();
    Check(!Is007ModeUnlocked(save), "007 mode starts locked");
    save.flag007 = 1;
    Check(Is007ModeUnlocked(save), "the flag unlocks it");
    save.flag007 = 0;
    for (int32_t level = 0; level < kSoloLevelCount - 1; level++) {
        SetPackedStageTime(save, level, kDifficulty00, 60);
    }
    Check(!Is007ModeUnlocked(save), "one level short on 00 Agent is not enough");
    SetPackedStageTime(save, kSoloLevelCount - 1, kDifficulty00, 60);
    Check(Is007ModeUnlocked(save), "all 20 levels on 00 Agent unlock it");
    save.flag007 = 2;
    SaveData other = BlankSave();
    other.flag007 = 2;
    Check(!Is007ModeUnlocked(other), "only the low bit of the flag counts");
}

} // namespace

int main() {
    TestTheBlankSave();
    TestLayoutIs96BytesBigEndian();
    TestChecksumsMatchReferenceValues();
    TestChecksumCoversEveryByteAfterTheChecksums();
    TestFolderSlotBondAndResetFlags();
    TestSaveFlagsSetMacro();
    TestStageTimesPackTenBitsPerEntry();
    TestEveryEntryHoldsItsOwnValue();
    TestStageTimeLimitsAndRanges();
    TestCheatBits();
    TestUnlockingEveryCheat();
    TestOptionMasks();
    TestSevenModeUnlock();
    return gFailures == 0 ? 0 : 1;
}
