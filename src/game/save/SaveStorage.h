#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frigate::Game::Save {

// The console's 4 Kbit EEPROM: 64 blocks of 8 bytes.
constexpr size_t kEepromBlockSize = 8;
constexpr size_t kEepromSize = 512;

// The EEPROM the decomp reaches through joyGamePakProbe/LongRead/LongWrite
// (osEepromProbe and friends), addressed in 8-byte blocks. Persistence lives
// behind this so the save logic can run without a file.
class SaveStorage {
  public:
    virtual ~SaveStorage() = default;

    // osEepromProbe() != 0.
    virtual bool Present() = 0;
    // osEepromLongRead; false when the image can't be read (missing or short).
    virtual bool Read(uint8_t block, uint8_t* buffer, size_t length) = 0;
    // osEepromLongWrite; false when the write didn't land.
    virtual bool Write(uint8_t block, const uint8_t* buffer, size_t length) = 0;
};

// An EEPROM image held in memory, for tests and headless runs.
class MemorySaveStorage : public SaveStorage {
  public:
    // A fresh chip has never been written: reads fail until the first write
    // (or Load), as a missing save file does.
    bool Present() override {
        return present_;
    }
    bool Read(uint8_t block, uint8_t* buffer, size_t length) override;
    bool Write(uint8_t block, const uint8_t* buffer, size_t length) override;

    void SetPresent(bool present) {
        present_ = present;
    }
    // Replaces the whole image (as if a save file had been put in place).
    void Load(const std::array<uint8_t, kEepromSize>& image);
    const std::array<uint8_t, kEepromSize>& Image() const {
        return image_;
    }
    // Whether anything has been written or loaded.
    bool HasImage() const {
        return hasImage_;
    }
    int WriteCount() const {
        return writeCount_;
    }
    // Makes later writes fail, as a full or read-only disk does.
    void SetWriteFails(bool fails) {
        writeFails_ = fails;
    }

  private:
    bool present_ = true;
    bool hasImage_ = false;
    bool writeFails_ = false;
    int writeCount_ = 0;
    std::array<uint8_t, kEepromSize> image_{};
};

} // namespace Frigate::Game::Save
