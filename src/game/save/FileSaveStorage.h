#pragma once

#include "game/save/SaveStorage.h"

namespace Frigate::Game::Save {

// The EEPROM image as libultraship keeps it: a 512-byte file in the app
// directory, through osEepromProbe/LongRead/LongWrite. Layout:
// docs/formats/goldeneye-eeprom-save.md.
class FileSaveStorage : public SaveStorage {
  public:
    bool Present() override;
    bool Read(uint8_t block, uint8_t* buffer, size_t length) override;
    bool Write(uint8_t block, const uint8_t* buffer, size_t length) override;
};

} // namespace Frigate::Game::Save
