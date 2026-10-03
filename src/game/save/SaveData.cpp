#include "game/save/SaveData.h"

#include "model/Random.h"

namespace Frigate::Game::Save {

namespace {

void PutBe32(uint8_t* out, int32_t value) {
    const auto bits = static_cast<uint32_t>(value);
    out[0] = static_cast<uint8_t>(bits >> 24);
    out[1] = static_cast<uint8_t>(bits >> 16);
    out[2] = static_cast<uint8_t>(bits >> 8);
    out[3] = static_cast<uint8_t>(bits);
}

int32_t GetBe32(const uint8_t* in) {
    return static_cast<int32_t>((static_cast<uint32_t>(in[0]) << 24) | (static_cast<uint32_t>(in[1]) << 16) |
                                (static_cast<uint32_t>(in[2]) << 8) | static_cast<uint32_t>(in[3]));
}

bool ValidStage(int32_t level, int32_t difficulty, int32_t difficultyEnd) {
    return level >= kLevelDam && level < kSoloLevelCount && difficulty >= kDifficultyAgent &&
           difficulty < difficultyEnd;
}

} // namespace

SaveData BlankSave() {
    SaveData save;
    save.completionBitflags = SaveFlagsSet(0, 0, kBondBrosnan, true);
    save.musicVol = 0xFF;
    save.sfxVol = 0xFF;
    save.options = kDefaultOptions;
    return save;
}

SaveBytes SerializeSave(const SaveData& save) {
    SaveBytes bytes{};
    PutBe32(&bytes[0], save.chksum1);
    PutBe32(&bytes[4], save.chksum2);
    bytes[8] = save.completionBitflags;
    bytes[9] = save.flag007;
    bytes[10] = save.musicVol;
    bytes[11] = save.sfxVol;
    bytes[12] = static_cast<uint8_t>(save.options >> 8);
    bytes[13] = static_cast<uint8_t>(save.options);
    for (size_t i = 0; i < save.unlockedCheats.size(); i++) {
        bytes[14 + i] = save.unlockedCheats[i];
    }
    bytes[17] = save.padding;
    for (size_t i = 0; i < save.times.size(); i++) {
        bytes[18 + i] = save.times[i];
    }
    bytes[94] = save.tail[0];
    bytes[95] = save.tail[1];
    return bytes;
}

SaveData DeserializeSave(const SaveBytes& bytes) {
    SaveData save;
    save.chksum1 = GetBe32(&bytes[0]);
    save.chksum2 = GetBe32(&bytes[4]);
    save.completionBitflags = bytes[8];
    save.flag007 = bytes[9];
    save.musicVol = bytes[10];
    save.sfxVol = bytes[11];
    save.options = static_cast<uint16_t>((bytes[12] << 8) | bytes[13]);
    for (size_t i = 0; i < save.unlockedCheats.size(); i++) {
        save.unlockedCheats[i] = bytes[14 + i];
    }
    save.padding = bytes[17];
    for (size_t i = 0; i < save.times.size(); i++) {
        save.times[i] = bytes[18 + i];
    }
    save.tail[0] = bytes[94];
    save.tail[1] = bytes[95];
    return save;
}

Checksums GenerateChecksums(const uint8_t* begin, const uint8_t* end) {
    uint64_t polynomial = 0x8F809F473108B3C1ull;
    int32_t shift = 0;
    uint32_t checksum1 = 0;
    uint32_t checksum2 = 0;

    for (const uint8_t* byte = begin; byte < end; byte++, shift += 7) {
        polynomial += static_cast<uint64_t>(static_cast<int64_t>(*byte << (shift & 0xF)));
        checksum1 ^= RandomGetNextFrom(&polynomial);
    }
    for (ptrdiff_t n = end - begin; n > 0; n--, shift += 3) {
        polynomial += static_cast<uint64_t>(static_cast<int64_t>(begin[n - 1] << (shift & 0xF)));
        checksum2 ^= RandomGetNextFrom(&polynomial);
    }
    return { static_cast<int32_t>(checksum1), static_cast<int32_t>(checksum2) };
}

Checksums SaveChecksums(const SaveData& save) {
    const SaveBytes bytes = SerializeSave(save);
    return GenerateChecksums(bytes.data() + kChecksumOffset, bytes.data() + bytes.size());
}

uint32_t GetSaveFolder(const SaveData& save) {
    return save.completionBitflags & kSaveFlagFolder;
}

void SetSaveFolder(SaveData& save, uint32_t folder) {
    save.completionBitflags &= static_cast<uint8_t>(~kSaveFlagFolder);
    save.completionBitflags |= static_cast<uint8_t>(folder & kSaveFlagFolder);
}

uint32_t GetSaveFlagSlot(const SaveData& save) {
    return (save.completionBitflags & kSaveFlagSlot) >> 3;
}

void SetSaveFlagSlot(SaveData& save, int32_t slot) {
    save.completionBitflags &= static_cast<uint8_t>(~kSaveFlagSlot);
    save.completionBitflags |= static_cast<uint8_t>((slot * 8) & kSaveFlagSlot);
}

uint32_t GetSelectedBond(const SaveData& save) {
    return (save.completionBitflags & kSaveFlagBond) >> 5;
}

void SetSelectedBond(SaveData& save, int32_t bond) {
    save.completionBitflags &= static_cast<uint8_t>(~kSaveFlagBond);
    save.completionBitflags |= static_cast<uint8_t>((bond << 5) & kSaveFlagBond);
}

bool GetSaveFlagDoReset(const SaveData& save) {
    return (save.completionBitflags & kSaveFlagDoReset) != 0;
}

void SetSaveFlagDoReset(SaveData& save, bool enable) {
    if (enable) {
        save.completionBitflags |= kSaveFlagDoReset;
    } else {
        save.completionBitflags &= static_cast<uint8_t>(~kSaveFlagDoReset);
    }
}

uint32_t GetPackedStageTime(const SaveData& save, int32_t level, int32_t difficulty) {
    if (!ValidStage(level, difficulty, kDifficulty007)) {
        return 0;
    }

    // Ten bits per level and difficulty, back to back from the first byte.
    const int32_t offset = ((difficulty * kSoloLevelCount) + level) * 10;
    const size_t index = static_cast<size_t>(offset >> 3);
    const auto& times = save.times;

    switch (7 - (offset & 7)) {
        case 7:
            return (static_cast<uint32_t>(times[index]) << 2) | ((times[index + 1] & 0xC0) >> 6);
        case 5:
            return (static_cast<uint32_t>(times[index] & 0x3F) << 4) | ((times[index + 1] & 0xF0) >> 4);
        case 3:
            return (static_cast<uint32_t>(times[index] & 0x0F) << 6) | ((times[index + 1] & 0xFC) >> 2);
        case 1:
            return (static_cast<uint32_t>(times[index] & 0x03) << 8) | times[index + 1];
        default:
            return 0;
    }
}

void SetPackedStageTime(SaveData& save, int32_t level, int32_t difficulty, int32_t newtime) {
    if (!ValidStage(level, difficulty, kDifficulty007)) {
        return;
    }

    if (newtime == 0) {
        newtime = 0x4F;
    } else if (newtime > 0x3FF) {
        newtime = 0x3FF;
    }

    const int32_t offset = ((difficulty * kSoloLevelCount) + level) * 10;
    const size_t index = static_cast<size_t>(offset >> 3);
    auto& times = save.times;

    switch (7 - (offset & 7)) {
        case 7:
            times[index] = 0;
            times[index + 1] &= 0x3F;
            times[index] |= (newtime >> 2) & 0xFF;
            times[index + 1] |= (newtime << 6) & 0xC0;
            break;
        case 5:
            times[index] &= 0xC0;
            times[index + 1] &= 0x0F;
            times[index] |= (newtime >> 4) & 0x3F;
            times[index + 1] |= (newtime << 4) & 0xF0;
            break;
        case 3:
            times[index] &= 0xF0;
            times[index + 1] &= 0x03;
            times[index] |= (newtime >> 6) & 0x0F;
            times[index + 1] |= (newtime << 2) & 0xFC;
            break;
        case 1:
            times[index] &= 0xFC;
            times[index + 1] = 0;
            times[index] |= (newtime >> 8) & 0x03;
            times[index + 1] |= newtime & 0xFF;
            break;
        default:
            break;
    }
}

bool IsCheatUnlocked(const SaveData& save, int32_t cheat) {
    if (cheat < 0 || cheat >= kCheatCount) {
        return false;
    }
    // The third byte is OR-ed in twice, as the decomp's expression has it; a
    // cheat below 20 only ever reaches its low four bits.
    const uint32_t bits =
        static_cast<uint32_t>(save.unlockedCheats[0]) | (static_cast<uint32_t>(save.unlockedCheats[2]) << 24) |
        (static_cast<uint32_t>(save.unlockedCheats[2]) << 16) | (static_cast<uint32_t>(save.unlockedCheats[1]) << 8);
    return ((1u << cheat) & bits) != 0;
}

void SetCheatUnlocked(SaveData& save, int32_t cheat) {
    if (cheat < 0 || cheat >= kCheatCount) {
        return;
    }
    uint32_t bit = 1u << cheat;
    size_t byteIndex = 0;
    while (bit > 0xFF) {
        bit >>= 8;
        byteIndex++;
    }
    save.unlockedCheats[byteIndex] |= static_cast<uint8_t>(bit & 0xFF);
}

bool Is007ModeUnlocked(const SaveData& save) {
    if (save.flag007 & 1) {
        return true;
    }
    for (int32_t level = kLevelDam; level < kSoloLevelCount; level++) {
        if (GetPackedStageTime(save, level, kDifficulty00) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace Frigate::Game::Save
