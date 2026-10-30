#pragma once

#include "extract/Error.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frigate::Extract {

// The texture bank's bitstream decoder. On-disk layout: docs/formats/goldeneye-texture-bitstream.md.
//
// Output is native N64-packed bytes, which is what libultraship's texture importers read a Fast::Texture's data as.
// Paletted images decode to index bytes plus a separate palette, as on the hardware.

enum class TexFormat : uint8_t {
    RGBA32 = 0,
    RGBA16 = 1,
    RGB24 = 2,
    RGB15 = 3,
    IA16 = 4,
    IA8 = 5,
    IA4 = 6,
    I8 = 7,
    I4 = 8,
    RGBA16_CI8 = 9,
    RGBA16_CI4 = 10,
    IA16_CI8 = 11,
    IA16_CI4 = 12,
};

// Fast::TextureType values.
enum class TextureType : uint32_t {
    RGBA32bpp = 1,
    RGBA16bpp = 2,
    Palette4bpp = 3,
    Palette8bpp = 4,
    Grayscale4bpp = 5,
    Grayscale8bpp = 6,
    GrayscaleAlpha4bpp = 7,
    GrayscaleAlpha8bpp = 8,
    GrayscaleAlpha16bpp = 9,
};

bool IsPaletted(TexFormat format);
TextureType TextureTypeOf(TexFormat format);

// The fields at the front of every texture, without the pixels.
struct TextureHeader {
    TexFormat format = TexFormat::RGBA32;
    uint32_t width = 0;
    uint32_t height = 0;
    // How many levels the game holds for the texture once loaded, and whether the bitstream carries the lower
    // ones itself.
    uint32_t lodCount = 0;
    bool hasExplicitLods = false;
    bool paletted = false;
};

struct DecodedTexture {
    TextureHeader header;
    TextureType textureType = TextureType::RGBA32bpp;
    std::vector<uint8_t> pixels;
    // CI4/CI8 only: the palette as the bitstream stores it (IA16 words for the IA16 formats), which the game's mip
    // generation searches, and as RGBA5551 words (an IA16 palette is approximated).
    std::vector<uint16_t> paletteNative;
    std::vector<uint16_t> paletteRgba5551;
};

Error ReadTextureHeader(std::span<const uint8_t> data, TextureHeader& header);
Error DecodeTexture(std::span<const uint8_t> data, DecodedTexture& texture);

} // namespace Frigate::Extract
