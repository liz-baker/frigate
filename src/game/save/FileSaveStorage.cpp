#include "game/save/FileSaveStorage.h"

#include "libultraship/libultra/eeprom.h"

namespace Frigate::Game::Save {

bool FileSaveStorage::Present() {
    return osEepromProbe(nullptr) != 0;
}

bool FileSaveStorage::Read(uint8_t block, uint8_t* buffer, size_t length) {
    return osEepromLongRead(nullptr, block, buffer, static_cast<int32_t>(length)) == 0;
}

bool FileSaveStorage::Write(uint8_t block, const uint8_t* buffer, size_t length) {
    // libultraship's write takes a mutable buffer but only reads it.
    return osEepromLongWrite(nullptr, block, const_cast<uint8_t*>(buffer), static_cast<int32_t>(length)) == 0;
}

} // namespace Frigate::Game::Save
