#include "game/save/SaveStorage.h"

#include <algorithm>

namespace Frigate::Game::Save {

bool MemorySaveStorage::Read(uint8_t block, uint8_t* buffer, size_t length) {
    const size_t offset = static_cast<size_t>(block) * kEepromBlockSize;
    if (!hasImage_ || offset + length > kEepromSize) {
        return false;
    }
    std::copy_n(image_.begin() + offset, length, buffer);
    return true;
}

bool MemorySaveStorage::Write(uint8_t block, const uint8_t* buffer, size_t length) {
    const size_t offset = static_cast<size_t>(block) * kEepromBlockSize;
    if (writeFails_ || offset + length > kEepromSize) {
        return false;
    }
    std::copy_n(buffer, length, image_.begin() + offset);
    hasImage_ = true;
    writeCount_++;
    return true;
}

void MemorySaveStorage::Load(const std::array<uint8_t, kEepromSize>& image) {
    image_ = image;
    hasImage_ = true;
}

} // namespace Frigate::Game::Save
