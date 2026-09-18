#pragma once

#include "ship/resource/Resource.h"

#include <array>
#include <cstdint>
#include <vector>

// GoldenEye 007's bitmap font-bank resource type ("GEFN") -- the fixed-layout
// binary format `assets/font/*.bin` segments carry (frigate issue #11).
// On-disk layout: docs/formats/goldeneye-font-text.md.
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

    // Raw glyph pixel bytes, undecoded. FontChar::pixelDataOffset indexes
    // into the full segment, not into this vector directly -- see
    // ResourceFactoryBinaryFontV0.
    // TODO(https://github.com/liz-baker/frigate/issues/18): decode into a
    // real pixel format instead of storing these opaque.
    std::vector<uint8_t> PixelData;
};

} // namespace Frigate
