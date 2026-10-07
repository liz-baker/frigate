#pragma once

#include <array>
#include <cstdint>

#include "game/MultiplayerSettings.h"

namespace Frigate::Game {

// The part of mp_chr_setup's rows the character select reads: the LTITLE slot
// of the character's name (text_preset) and which of the sixteen portraits
// (MPCHRSELIMAGES, select_photo) it shows. The rows' body, head and gender
// belong to the stage's use of the choice.
struct MpCharacterEntry {
    uint16_t nameSlot;
    uint8_t photo;
};

constexpr int kMpPortraitCount = 16;

constexpr std::array<MpCharacterEntry, MultiplayerSettings::kCharacterRows> kMpCharacters = { {
    { 184, 0 },  { 191, 9 },  { 188, 6 },  { 190, 8 },  { 187, 5 },  { 186, 4 },  { 189, 7 },  { 185, 15 },
    { 194, 12 }, { 193, 11 }, { 195, 13 }, { 192, 10 }, { 253, 14 }, { 252, 14 }, { 263, 14 }, { 263, 14 },
    { 256, 14 }, { 254, 14 }, { 257, 14 }, { 262, 14 }, { 251, 14 }, { 264, 14 }, { 264, 14 }, { 264, 14 },
    { 264, 14 }, { 258, 14 }, { 259, 14 }, { 258, 14 }, { 260, 14 }, { 250, 14 }, { 255, 14 }, { 261, 14 },
    { 261, 14 }, { 196, 14 }, { 197, 14 }, { 198, 14 }, { 199, 14 }, { 200, 14 }, { 201, 14 }, { 202, 14 },
    { 203, 14 }, { 204, 14 }, { 205, 14 }, { 206, 14 }, { 207, 14 }, { 208, 14 }, { 209, 14 }, { 210, 14 },
    { 211, 14 }, { 212, 14 }, { 213, 14 }, { 214, 14 }, { 215, 14 }, { 216, 14 }, { 217, 14 }, { 218, 14 },
    { 219, 14 }, { 220, 14 }, { 221, 14 }, { 222, 14 }, { 223, 14 }, { 224, 14 }, { 225, 14 }, { 226, 14 },
} };

} // namespace Frigate::Game
