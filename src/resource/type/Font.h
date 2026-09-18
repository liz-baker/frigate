#pragma once

#include "ship/resource/Resource.h"

#include <array>
#include <cstdint>
#include <vector>

// GoldenEye 007's bitmap font-bank resource type ("GEFN") -- the fixed-layout
// binary format `assets/font/*.bin` segments carry (frigate issue #11).
// Transcribed from external/007's `struct font`/`struct fontchar`
// (src/bondtypes.h) and the source form of a font bank (assets/font/
// fontBankGothic.c's `_kerning`/`_fontchartable`/`_fontbytes` arrays) --
// only the bare struct-layout facts are used here, in fresh
// types/wording, per CLAUDE.md's licensing rules for external/007.
//
// On-disk layout (big-endian, matching the ROM's native MIPS byte order --
// external/007 never byte-swaps these segments at runtime; see
// src/game/textrelated.c's load_font_tables(), a straight DMA/memcpy):
//
//   offset 0                   : kerning table, kKerningTableSize (13x13)
//                                 big-endian s32 values
//   offset kKerningByteSize    : char table, kNumChars entries, 24 bytes
//                                 each -- 6 big-endian fields (index,
//                                 baseline, height, width, kerningIndex,
//                                 pixelDataOffset)
//   offset kKerningByteSize +
//          kNumChars*24        : raw glyph pixel bytes. Each char's
//                                 pixelDataOffset points into this region,
//                                 file-relative from the segment's own
//                                 start (not from the pixel region's start
//                                 -- external/007's runtime fixup adds the
//                                 segment's base pointer to this field
//                                 directly). The pixel byte format itself
//                                 isn't decoded here -- see PixelData.
//
// kNumChars is fixed at the NTSC glyph count (94); external/007 notes PAL
// fonts use 135 instead. Not handled here since this port targets the USA
// ROM (see README.md).
namespace Frigate {

struct FontChar {
    int32_t index = 0;
    int32_t baseline = 0;
    int32_t height = 0;
    int32_t width = 0;
    int32_t kerningIndex = 0;
    uint32_t pixelDataOffset = 0; // file-relative from the segment start; see module comment
};

class Font final : public Ship::Resource<FontChar> {
  public:
    using Resource::Resource;

    Font();

    FontChar* GetPointer() override;
    size_t GetPointerSize() override;

    static constexpr size_t kKerningTableSize = 13 * 13;
    static constexpr size_t kNumChars = 94;

    std::array<int32_t, kKerningTableSize> Kerning{};
    std::vector<FontChar> Chars;

    // Raw glyph pixel bytes, undecoded (see module comment -- pixel format
    // isn't confirmed yet). FontChar::pixelDataOffset indexes into the full
    // segment, not into this vector directly -- see ResourceFactoryBinaryFontV0.
    std::vector<uint8_t> PixelData;
};

} // namespace Frigate
