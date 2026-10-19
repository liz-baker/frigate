#pragma once

#include "extract/Texture.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Frigate::Extract {

// The lower mip levels the game generates when it loads a texture. Chain layout:
// docs/formats/goldeneye-texture-mip-chain.md.

// The bytes of the base level plus every generated level must fit this for a paletted image.
constexpr size_t kPalettedChainLimit = 0x800;

struct MipLevel {
    uint32_t width;
    uint32_t height;
    // Rows padded to whole 64-bit TMEM words (AlignedRowTexels).
    std::vector<uint8_t> data;
};

uint32_t AlignedRowTexels(TexFormat format, uint32_t width);
std::vector<uint8_t> PadRows(TexFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> data);

// The game's closest-palette-entry searches: an exact match if there is one, else a binary search by squared
// magnitude and a distance check of the entries either side of where it stops.
size_t ClosestRgbaIndex(std::span<const uint16_t> palette, int r, int g, int b, int a);
size_t ClosestIaIndex(std::span<const uint16_t> palette, int intensity, int alpha);

// `palette` is the image's own entries as the game stores them (IA16 words for the IA formats).
std::vector<uint8_t> ShrinkPaletted(TexFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> src,
                                    std::span<const uint16_t> palette);
std::vector<uint8_t> ShrinkNonPaletted(TexFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> src);

// The levels the game holds for `texture` once loaded, base level first, or nullopt when it holds only the base
// level (including 32-bit images, which get no chain).
std::optional<std::vector<MipLevel>> BuildMipChain(const DecodedTexture& texture);

} // namespace Frigate::Extract
