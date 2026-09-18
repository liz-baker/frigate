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
    // Rows are padded to a multiple of 8 pixels.
    static constexpr int32_t RowStride(int32_t width) {
        return (width + 7) & ~7;
    }

    int32_t index = 0;
    int32_t baseline = 0;
    int32_t height = 0;
    int32_t width = 0;
    int32_t kerningIndex = 0;
    uint32_t pixelDataOffset = 0; // file-relative from the segment start
    std::vector<uint8_t> Pixels;  // 8-bit intensity, row-major, RowStride(width) bytes per row
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
};

} // namespace Frigate
