// Tests for the texture bank and image packers. The decoder and mip tests run on hand-built bitstreams; the stage
// tests run on a synthetic ROM image; the last check runs the whole texture set against a real US ROM when
// FRIGATE_ROMS (a path-separator list of ROM files and directories) points at one.

#include "extract/DisplayListEmit.h"
#include "extract/Extractor.h"
#include "extract/ImageDefs.h"
#include "extract/Inflate.h"
#include "extract/Texture.h"
#include "extract/TextureMip.h"
#include "extract/TexturePackers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

std::vector<uint8_t> FromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

// MSB-first, the writer matching the decoder's reader.
class BitWriter {
  public:
    BitWriter& Write(uint32_t value, unsigned bits) {
        for (int i = int(bits) - 1; i >= 0; i--) {
            mBits.push_back((value >> i) & 1);
        }
        return *this;
    }
    BitWriter& Extend(const BitWriter& other) {
        mBits.insert(mBits.end(), other.mBits.begin(), other.mBits.end());
        return *this;
    }
    std::vector<uint8_t> Bytes() const {
        std::vector<uint8_t> out;
        for (size_t i = 0; i < mBits.size(); i += 8) {
            uint8_t byte = 0;
            for (size_t j = 0; j < 8; j++) {
                byte = uint8_t((byte << 1) | (i + j < mBits.size() ? mBits[i + j] : 0));
            }
            out.push_back(byte);
        }
        return out;
    }

  private:
    std::vector<int> mBits;
};

BitWriter HeaderBits(bool paletted, unsigned explicitLods = 0, unsigned lod = 0) {
    BitWriter w;
    w.Write(explicitLods, 1).Write(paletted ? 1 : 0, 1).Write(lod, 6);
    return w;
}

BitWriter PlainHeader(TexFormat format, unsigned width, unsigned height, unsigned compression) {
    BitWriter w = HeaderBits(false);
    w.Write(uint32_t(format), 4).Write(width, 8).Write(height, 8).Write(compression, 4);
    return w;
}

DecodedTexture Decode(const std::vector<uint8_t>& data, bool* ok = nullptr) {
    DecodedTexture texture;
    Error err = DecodeTexture(data, texture);
    if (ok != nullptr) {
        *ok = err.ok();
    }
    return texture;
}

std::vector<uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<uint8_t> out;
    for (int v : values) {
        out.push_back(uint8_t(v));
    }
    return out;
}

void TestUncompressed() {
    {
        BitWriter w = PlainHeader(TexFormat::RGBA32, 2, 1, 0);
        for (int v : { 0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD }) {
            w.Write(uint32_t(v), 8);
        }
        DecodedTexture t = Decode(w.Bytes());
        Check(t.header.format == TexFormat::RGBA32 && t.header.width == 2 && t.header.height == 1, "rgba32 header");
        Check(t.textureType == TextureType::RGBA32bpp, "rgba32 type");
        Check(t.pixels == Bytes({ 0x11, 0x22, 0x33, 0x44, 0xAA, 0xBB, 0xCC, 0xDD }), "rgba32 pixels");
        Check(t.paletteNative.empty(), "rgba32 has no palette");
    }
    {
        BitWriter w = PlainHeader(TexFormat::RGBA16, 2, 1, 0);
        w.Write(0x1F, 5).Write(0, 5).Write(0xA, 5).Write(1, 1);
        w.Write(0, 5).Write(0x1F, 5).Write(0, 5).Write(0, 1);
        DecodedTexture t = Decode(w.Bytes());
        Check(t.textureType == TextureType::RGBA16bpp, "rgba16 type");
        Check(t.pixels == Bytes({ 0xF8, 0x15, 0x07, 0xC0 }), "rgba16 pixels");
    }
    {
        BitWriter w = PlainHeader(TexFormat::IA8, 2, 1, 0);
        w.Write(0xA, 4).Write(0x5, 4).Write(0x1, 4).Write(0xF, 4);
        DecodedTexture t = Decode(w.Bytes());
        Check(t.textureType == TextureType::GrayscaleAlpha8bpp && t.pixels == Bytes({ 0xA5, 0x1F }), "ia8");
    }
    {
        // Odd width: the last nibble of each row is padded, never carried into the next row.
        BitWriter w = PlainHeader(TexFormat::I4, 3, 2, 0);
        for (int v : { 1, 2, 3, 4, 5, 6 }) {
            w.Write(uint32_t(v), 4);
        }
        DecodedTexture t = Decode(w.Bytes());
        Check(t.textureType == TextureType::Grayscale4bpp, "i4 type");
        Check(t.pixels == Bytes({ 0x12, 0x30, 0x45, 0x60 }), "i4 odd width resets per row");
    }
    {
        BitWriter w = PlainHeader(TexFormat::IA4, 2, 1, 0);
        w.Write(5, 3).Write(1, 1).Write(2, 3).Write(0, 1);
        DecodedTexture t = Decode(w.Bytes());
        Check(t.textureType == TextureType::GrayscaleAlpha4bpp && t.pixels == Bytes({ ((5 << 1 | 1) << 4) | (2 << 1) }),
              "ia4");
    }
    {
        BitWriter w = PlainHeader(TexFormat::RGB24, 1, 1, 0);
        w.Write(0x10, 8).Write(0x20, 8).Write(0x30, 8);
        DecodedTexture t = Decode(w.Bytes());
        Check(t.textureType == TextureType::RGBA32bpp && t.pixels == Bytes({ 0x10, 0x20, 0x30, 0xFF }),
              "rgb24 forces alpha");
    }
    {
        BitWriter w = PlainHeader(TexFormat::RGB15, 1, 1, 0);
        w.Write(0x1F, 5).Write(0, 5).Write(0xA, 5);
        DecodedTexture t = Decode(w.Bytes());
        Check(t.pixels == Bytes({ 0xF8, 0x15 }), "rgb15 forces the alpha bit");
    }
}

void TestCompressed() {
    {
        // I4 with a Huffman table (frequencies 10, 5, 1 for symbols 1, 2, 3), made by the Python encoder.
        DecodedTexture t = Decode(FromHex("00803022000a05010000000000000000000000004e20"));
        Check(t.pixels == Bytes({ 0x12, 0x10, 0x31, 0x20 }), "huffman");
    }
    {
        DecodedTexture t = Decode(FromHex("0070401600615906e120602580"));
        Check(t.pixels == Bytes({ 0x0A, 0x0A, 0xC8, 0x37 }), "huffman over a lookup table");
    }
    {
        // RLE, literals only.
        BitWriter w = PlainHeader(TexFormat::I8, 4, 1, 4);
        w.Write(3, 3).Write(3, 3).Write(8, 4);
        for (int v : { 5, 250, 0, 128 }) {
            w.Write(0, 1).Write(uint32_t(v), 8);
        }
        Check(Decode(w.Bytes()).pixels == Bytes({ 5, 250, 0, 128 }), "rle literals");
    }
    {
        // With 0 back bits, 0 run bits and 8 value bits the run is 1 long; it copies the literal before it and
        // is followed by a literal with no directive bit of its own.
        BitWriter w = PlainHeader(TexFormat::I8, 3, 1, 4);
        w.Write(0, 3).Write(0, 3).Write(8, 4);
        w.Write(0, 1).Write(42, 8);
        w.Write(1, 1).Write(99, 8);
        Check(Decode(w.Bytes()).pixels == Bytes({ 42, 42, 99 }), "rle run");
    }
    {
        BitWriter w = PlainHeader(TexFormat::I8, 4, 1, 5);
        w.Write(3, 11).Write(10, 8).Write(200, 8).Write(55, 8);
        for (int i : { 0, 2, 1, 0 }) {
            w.Write(uint32_t(i), 2);
        }
        Check(Decode(w.Bytes()).pixels == Bytes({ 10, 55, 200, 10 }), "lookup");
    }
    {
        BitWriter w = PlainHeader(TexFormat::I4, 3, 1, 5);
        w.Write(2, 11).Write(0x3, 4).Write(0xC, 4);
        for (int i : { 1, 0, 1 }) {
            w.Write(uint32_t(i), 1);
        }
        Check(Decode(w.Bytes()).pixels == Bytes({ 0xC3, 0xC0 }), "lookup of 4-bit entries");
    }
    {
        BitWriter w = PlainHeader(TexFormat::I8, 3, 1, 5);
        w.Write(1, 11).Write(0x7F, 8);
        Check(Decode(w.Bytes()).pixels == Bytes({ 0x7F, 0x7F, 0x7F }), "a one-entry table reads no index bits");
    }
    {
        // The filter predicts from already-reconstructed neighbours: method 0 over equal deltas is a running sum.
        BitWriter w = PlainHeader(TexFormat::I4, 3, 1, 9);
        w.Write(0, 3);
        w.Write(3, 3).Write(3, 3).Write(4, 4);
        for (int i = 0; i < 3; i++) {
            w.Write(0, 1).Write(1, 4);
        }
        Check(Decode(w.Bytes()).pixels == Bytes({ 0x12, 0x30 }), "blur reads the reconstructed row");
    }
    {
        // Method 4 at (1,1): above 4, above-left 9, so the halved difference is -2 (C truncates toward zero), not -3.
        BitWriter w = PlainHeader(TexFormat::I8, 2, 2, 9);
        w.Write(4, 3);
        w.Write(3, 3).Write(3, 3).Write(8, 4);
        for (int v : { 9, 251, 0, 0 }) {
            w.Write(0, 1).Write(uint32_t(v), 8);
        }
        Check(Decode(w.Bytes()).pixels == Bytes({ 9, 4, 4, 2 }), "blur halves toward zero");
    }
}

std::vector<uint8_t> RzipPalettedTexture(TexFormat format, std::vector<uint16_t> palette, unsigned width,
                                         unsigned height, const std::vector<uint8_t>& indices,
                                         unsigned explicitLods = 0, unsigned lod = 0) {
    BitWriter w = HeaderBits(true, explicitLods, lod);
    w.Write(uint32_t(format), 8).Write(uint32_t(palette.size() - 1), 8);
    for (uint16_t c : palette) {
        w.Write(c, 16);
    }
    w.Write(width, 8).Write(height, 8);
    std::vector<uint8_t> out = w.Bytes();
    std::vector<uint8_t> deflated;
    DeflateRaw(indices, deflated, 9);
    out.push_back(0x11);
    out.push_back(0x72);
    out.insert(out.end(), deflated.begin(), deflated.end());
    return out;
}

void TestPaletted() {
    {
        DecodedTexture t =
            Decode(RzipPalettedTexture(TexFormat::RGBA16_CI8, { 0xF801, 0x07C1 }, 2, 2, Bytes({ 0, 1, 1, 0 })));
        Check(t.header.format == TexFormat::RGBA16_CI8 && t.textureType == TextureType::Palette8bpp, "ci8 type");
        Check(t.pixels == Bytes({ 0, 1, 1, 0 }), "ci8 indices");
        Check(t.paletteRgba5551 == std::vector<uint16_t>({ 0xF801, 0x07C1 }), "ci8 palette");
    }
    {
        // Already nibble-packed, as the game stores them.
        DecodedTexture t = Decode(RzipPalettedTexture(TexFormat::RGBA16_CI4, { 0x1234, 0x5678, 0x9ABC, 0xDEF1 }, 4, 1,
                                                      Bytes({ 0x01, 0x23 })));
        Check(t.textureType == TextureType::Palette4bpp && t.pixels == Bytes({ 0x01, 0x23 }), "ci4");
    }
    {
        // An IA16 palette entry is normalized to RGBA5551 for the importers; the native word stays for mip
        // generation.
        DecodedTexture t = Decode(RzipPalettedTexture(TexFormat::IA16_CI8, { 0xF8FF }, 1, 1, Bytes({ 0 })));
        Check(t.paletteRgba5551 == std::vector<uint16_t>({ 0xFFFF }), "ia16 palette is normalized");
        Check(t.paletteNative == std::vector<uint16_t>({ 0xF8FF }), "ia16 palette keeps its native words");
    }
    {
        DecodedTexture t = Decode(RzipPalettedTexture(TexFormat::RGBA16_CI8, { 1 }, 1, 1, Bytes({ 0 }), 1, 5));
        Check(t.header.lodCount == 5 && t.header.hasExplicitLods, "level count and explicit bit");
    }
    {
        TextureHeader header;
        std::vector<uint8_t> data = RzipPalettedTexture(TexFormat::IA16_CI4, { 1, 2 }, 40, 12, Bytes({ 0 }));
        Check(ReadTextureHeader(data, header).ok() && header.format == TexFormat::IA16_CI4 && header.width == 40 &&
                  header.height == 12 && header.paletted,
              "header read without decoding");
    }
    {
        BitWriter w = HeaderBits(true);
        w.Write(uint32_t(TexFormat::RGBA32), 8);
        bool ok = true;
        Decode(w.Bytes(), &ok);
        Check(!ok, "a non-paletted format in the paletted branch is rejected");
        Decode({}, &ok);
        Check(!ok, "empty data is rejected");
        Decode(Bytes({ 0x00, 0x70 }), &ok);
        Check(!ok, "truncated data is rejected");
    }
}

// --- mip chains -------------------------------------------------------------------------------------------------

uint16_t Rgba5551(int r, int g, int b, int a) {
    return uint16_t((r << 11) | (g << 6) | (b << 1) | a);
}

std::vector<uint8_t> Rgba16Image(const std::vector<uint16_t>& texels, uint32_t width, uint32_t height) {
    uint32_t aligned = AlignedRowTexels(TexFormat::RGBA16, width);
    std::vector<uint8_t> out(aligned * height * 2, 0);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            out[(y * aligned + x) * 2] = uint8_t(texels[y * width + x] >> 8);
            out[(y * aligned + x) * 2 + 1] = uint8_t(texels[y * width + x]);
        }
    }
    return out;
}

uint16_t Texel16(const std::vector<uint8_t>& data, uint32_t aligned, uint32_t x, uint32_t y) {
    return uint16_t((data[(y * aligned + x) * 2] << 8) | data[(y * aligned + x) * 2 + 1]);
}

DecodedTexture MipInput(TexFormat format, uint32_t width, uint32_t height, size_t bytes, uint32_t lods,
                        std::vector<uint16_t> palette = {}) {
    DecodedTexture t;
    t.header.format = format;
    t.header.width = width;
    t.header.height = height;
    t.header.lodCount = lods;
    t.pixels.assign(bytes, 0);
    t.paletteNative = std::move(palette);
    return t;
}

void TestMip() {
    Check(AlignedRowTexels(TexFormat::RGBA16, 5) == 8 && AlignedRowTexels(TexFormat::I8, 9) == 16 &&
              AlignedRowTexels(TexFormat::RGBA16_CI4, 5) == 16 && AlignedRowTexels(TexFormat::RGBA32, 1) == 4,
          "rows pad to whole TMEM words");
    Check(PadRows(TexFormat::RGBA16_CI4, 5, 2, Bytes({ 1, 2, 3, 4, 5, 6 })) ==
              Bytes({ 1, 2, 3, 0, 0, 0, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0 }),
          "pad_rows keeps each row at its padded offset");

    {
        std::vector<uint16_t> texels = { Rgba5551(31, 8, 1, 1), Rgba5551(31, 8, 2, 1), Rgba5551(0, 8, 3, 0),
                                         Rgba5551(0, 9, 4, 0) };
        auto out = ShrinkNonPaletted(TexFormat::RGBA16, 2, 2, Rgba16Image(texels, 2, 2));
        Check(out.size() == 8 && Texel16(out, 4, 0, 0) == Rgba5551(15, 8, 2, 1), "rgba16 average, alpha half up");
        std::vector<uint16_t> one = { Rgba5551(0, 0, 0, 1), 0, 0, 0 };
        out = ShrinkNonPaletted(TexFormat::RGBA16, 2, 2, Rgba16Image(one, 2, 2));
        Check((Texel16(out, 4, 0, 0) & 1) == 0, "rgba16 alpha needs two of four");
        std::vector<uint16_t> odd = { Rgba5551(0, 0, 0, 1), Rgba5551(0, 0, 0, 1), Rgba5551(20, 0, 0, 1) };
        out = ShrinkNonPaletted(TexFormat::RGBA16, 3, 1, Rgba16Image(odd, 3, 1));
        Check(Texel16(out, 4, 1, 0) == Rgba5551(20, 0, 0, 1), "an odd width samples the last column twice");
    }
    {
        auto out = ShrinkNonPaletted(TexFormat::I8, 2, 2, PadRows(TexFormat::I8, 2, 2, Bytes({ 1, 2, 3, 4 })));
        Check(out[0] == ((1 + 2 + 3 + 4 + 1) >> 2) && out.size() == 8, "i8 rounds half up");
        out = ShrinkNonPaletted(TexFormat::IA8, 2, 2, PadRows(TexFormat::IA8, 2, 2, Bytes({ 0xF1, 0xF1, 0x10, 0x10 })));
        Check(out[0] == 0x80, "ia8 scales intensity and rounds alpha");
        out = ShrinkNonPaletted(TexFormat::IA16, 2, 2,
                                PadRows(TexFormat::IA16, 2, 2, Bytes({ 0xFF, 0xFF, 0xFF, 0xFF, 0, 1, 0, 0 })));
        Check(Texel16(out, 4, 0, 0) == ((127 << 8) | 128), "ia16 averages both bytes");
        out = ShrinkNonPaletted(TexFormat::I4, 4, 2, PadRows(TexFormat::I4, 4, 2, Bytes({ 0x31, 0x22, 0x11, 0x44 })));
        Check(out[0] == ((((3 + 1 + 1 + 1) << 2) & 0xF0) | (((2 + 2 + 4 + 4) >> 2) & 0xF)),
              "i4 pairs bytes like the game");
        out = ShrinkNonPaletted(TexFormat::IA4, 4, 2, PadRows(TexFormat::IA4, 4, 2, Bytes({ 0xFF, 0x00, 0xFF, 0x00 })));
        Check(out[0] == (((7 * 4) << 3 & 0xE0) | (5 << 2 & 0x10) | ((1 >> 2) & 1)), "ia4 bit layout");
        out = ShrinkNonPaletted(TexFormat::RGBA16, 8, 5, Rgba16Image(std::vector<uint16_t>(40, 0), 8, 5));
        Check(out.size() == 3 * AlignedRowTexels(TexFormat::RGBA16, 4) * 2,
              "level size is padded rows times half height");
    }
    {
        std::vector<uint16_t> palette = { Rgba5551(0, 0, 0, 1), Rgba5551(8, 8, 8, 1), Rgba5551(16, 16, 16, 1),
                                          Rgba5551(31, 31, 31, 1) };
        Check(ClosestRgbaIndex(palette, 16, 16, 16, 1) == 2, "exact palette match");
        Check(ClosestRgbaIndex(palette, 9, 7, 9, 1) == 1, "nearest palette entry");
        std::vector<uint16_t> alpha = { Rgba5551(10, 10, 10, 1), Rgba5551(20, 20, 20, 0) };
        Check(ClosestRgbaIndex(alpha, 10, 10, 10, 0) == 1, "an alpha mismatch outweighs colour");
        std::vector<uint16_t> ia = { 0x0000, 0x40FF, 0x80FF, 0xFFFF };
        Check(ClosestIaIndex(ia, 0x42, 0xF0) == 1 && ClosestIaIndex(ia, 0x80, 0xFF) == 2, "ia nearest entry");
    }
    {
        std::vector<uint16_t> palette = { Rgba5551(0, 0, 0, 1), Rgba5551(31, 31, 31, 1), Rgba5551(15, 15, 15, 1) };
        auto out = ShrinkPaletted(TexFormat::RGBA16_CI8, 2, 2,
                                  PadRows(TexFormat::RGBA16_CI8, 2, 2, Bytes({ 0, 1, 0, 1 })), palette);
        Check(out[0] == 2 && out.size() == 8, "ci8 maps the average back into the palette");
        out = ShrinkPaletted(TexFormat::RGBA16_CI4, 4, 2,
                             PadRows(TexFormat::RGBA16_CI4, 4, 2, Bytes({ 0x01, 0x11, 0x01, 0x11 })), palette);
        Check(out[0] == ((2 << 4) | 1), "ci4 packs two indices a byte");

        // The IA path's precedence slip: a nonzero source byte reads entry 0 for its right-hand texels.
        std::vector<uint16_t> ia(16, 0xFFFF);
        ia[0] = 0;
        out = ShrinkPaletted(TexFormat::IA16_CI4, 4, 2,
                             PadRows(TexFormat::IA16_CI4, 4, 2, Bytes({ 0x11, 0x11, 0x11, 0x11 })), ia);
        Check(out[0] == 0x00, "ia ci4 right texels read entry 0");
        out = ShrinkPaletted(TexFormat::IA16_CI4, 4, 2,
                             PadRows(TexFormat::IA16_CI4, 4, 2, Bytes({ 0x01, 0x01, 0x01, 0x01 })),
                             std::vector<uint16_t>{ 0x0000, 0xFFFF });
        Check(out[0] == 0x00, "ia ci4 entry 4 of a small palette reads as 0");
    }
    {
        Check(!BuildMipChain(MipInput(TexFormat::I8, 4, 4, 16, 0)) &&
                  !BuildMipChain(MipInput(TexFormat::I8, 4, 4, 16, 1)),
              "no chain below two levels");
        auto chain = BuildMipChain(MipInput(TexFormat::RGBA16, 8, 8, 128, 4));
        Check(chain && chain->size() == 4 && (*chain)[1].width == 4 && (*chain)[3].height == 1 &&
                  (*chain)[0].data.size() == 128 && (*chain)[1].data.size() == 32 && (*chain)[2].data.size() == 16 &&
                  (*chain)[3].data.size() == 8,
              "levels halve with padded rows");
        chain = BuildMipChain(MipInput(TexFormat::I8, 5, 3, 15, 3));
        Check(chain && chain->size() == 3 && (*chain)[1].width == 3 && (*chain)[1].height == 2 &&
                  (*chain)[2].width == 2 && (*chain)[2].height == 1,
              "odd sizes round up");
        std::vector<uint16_t> palette = { Rgba5551(0, 0, 0, 1) };
        Check(!BuildMipChain(MipInput(TexFormat::RGBA16_CI8, 40, 40, 1600, 4, palette)),
              "a paletted chain over the limit");
        chain = BuildMipChain(MipInput(TexFormat::RGBA16_CI8, 32, 32, 1024, 6, palette));
        Check(chain && chain->size() == 6, "a paletted chain under the limit");
        chain = BuildMipChain(MipInput(TexFormat::RGBA16_CI8, 32, 48, 1536, 5, palette));
        Check(chain && chain->size() == 3, "a paletted chain stops at the level that overflows");
        Check(!BuildMipChain(MipInput(TexFormat::RGBA32, 4, 4, 64, 3)), "32-bit images get no chain");
    }
}

// --- stages on a synthetic ROM ----------------------------------------------------------------------------------

void Put32(std::vector<uint8_t>& rom, uint64_t at, uint32_t value) {
    rom[at] = uint8_t(value >> 24);
    rom[at + 1] = uint8_t(value >> 16);
    rom[at + 2] = uint8_t(value >> 8);
    rom[at + 3] = uint8_t(value);
}

// A 1x1 8-bit intensity texture, enough for a binding.
void PutImage(std::vector<uint8_t>& rom, size_t index) {
    BitWriter w = PlainHeader(TexFormat::I8, 1, 1, 0);
    w.Write(0x55, 8);
    std::vector<uint8_t> bytes = w.Bytes();
    std::copy(bytes.begin(), bytes.end(), rom.begin() + ImageRomOffset(Region::Us, index));
}

std::map<std::string, std::vector<uint8_t>> ReadZip(const fs::path& path, bool& ok) {
    std::map<std::string, std::vector<uint8_t>> members;
    ok = false;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t at) { return uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8); };
    auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    if (data.size() < 22 || u32(data.size() - 22) != 0x06054B50) {
        return members;
    }
    size_t eocd = data.size() - 22;
    size_t count = u16(eocd + 10);
    size_t at = u32(eocd + 16);
    for (size_t i = 0; i < count; i++) {
        if (u32(at) != 0x02014B50) {
            return members;
        }
        uint32_t method = u16(at + 10), crc = u32(at + 16), csize = u32(at + 20), size = u32(at + 24);
        size_t nameLength = u16(at + 28), extraLength = u16(at + 30), commentLength = u16(at + 32);
        size_t local = u32(at + 42);
        std::string name(data.begin() + at + 46, data.begin() + at + 46 + nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        size_t bodyAt = local + 30 + u16(local + 26) + u16(local + 28);
        std::span<const uint8_t> body(data.data() + bodyAt, csize);
        std::vector<uint8_t> bytes;
        if (method == 8) {
            if (!InflateRaw(body, bytes, size).ok() && size != 0) {
                return members;
            }
        } else {
            bytes.assign(body.begin(), body.end());
        }
        if (bytes.size() != size || Crc32(bytes) != crc) {
            return members;
        }
        members[name] = bytes;
    }
    ok = true;
    return members;
}

fs::path ScratchDir(const char* name) {
    fs::path dir = fs::temp_directory_path() / (std::string("frigate_extract_texture_test_") + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// Runs one stage over `rom` and returns what it wrote.
std::map<std::string, std::vector<uint8_t>> RunStage(const std::function<Error(StageContext&, O2rWriter&)>& stage,
                                                     const std::vector<uint8_t>& rom, Region region, Error& error) {
    fs::path dir = ScratchDir("stage");
    O2rWriter writer;
    writer.Open(dir / "out.o2r");
    StageContext context(region, rom, nullptr, nullptr);
    error = stage(context, writer);
    writer.Close();
    bool ok = false;
    auto members = ReadZip(dir / "out.o2r", ok);
    fs::remove_all(dir);
    return members;
}

uint32_t Word(const std::vector<uint8_t>& data, size_t at) {
    return (uint32_t(data[at]) << 24) | (uint32_t(data[at + 1]) << 16) | (uint32_t(data[at + 2]) << 8) | data[at + 3];
}

uint64_t Word64(const std::vector<uint8_t>& data, size_t at) {
    return (uint64_t(Word(data, at)) << 32) | Word(data, at + 4);
}

constexpr size_t kPayload = kOtrHeaderSize; // a member's payload follows the 64-byte resource header

void TestPathHash() {
    Check(PathHash("textures/T200") == 0xd32254f00c5908edull, "path hash matches libultraship's CRC64");
    Check(PathHash("rarewarelogo/vtx/000100") == 0xdece8eb62a46cb0aull, "path hash of a vertex path");
}

void TestSkyAndHudStages() {
    const RegionConstants& constants = GetRomLayout(Region::Us).constants;
    std::vector<uint8_t> rom(kRomSize, 0);
    const uint32_t table = constants.hudImageTableRomStart;

    // Sky: texture ids 200..202, 64x64 intensity images wrapping in both axes; the last has levels.
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t at = table + constants.textures.skyTableOffset + i * 12;
        Put32(rom, at, 200 + i);
        rom[at + 4] = 64;
        rom[at + 5] = 64;
        rom[at + 6] = i == 2 ? 6 : 0;
        rom[at + 7] = 4;
        rom[at + 8] = 1;
        PutImage(rom, 200 + i);
    }
    Error error;
    auto members = RunStage(PackSkyImages, rom, Region::Us, error);
    Check(error.ok() && members.size() == 3 && members.count("sky/img/0") && members.count("sky/img/2"), "sky members");
    if (members.count("sky/img/0")) {
        const auto& m = members["sky/img/0"];
        Check(Word(m, kPayload + 8) == ((kG_SETTEX << 24) | (2 << 18) | 4) && Word(m, kPayload + 12) == 200,
              "a plain sky image is a single tile with a sample offset");
        Check(Word(m, kPayload + 16) == ((kOtrG_SETTIMG_HASH << 24) | (4u << 21) | (1u << 19) | 0) &&
                  Word64(m, kPayload + 24) == PathHash(std::string("textures/") + ImageDefs()[200].name),
              "the sky image names its texture resource");
    }
    if (members.count("sky/img/2")) {
        Check((Word(members["sky/img/2"], kPayload + 8) & 7) == 2, "a mipmapped sky image is a mipmap request");
    }

    // HUD: every table entry the packer reads is a 8x8 image of texture 100 (an effect range gets level 3), and
    // the 17 global lists each load one image.
    PutImage(rom, 100);
    const TextureConstants& layout = constants.textures;
    size_t entryCount = 0;
    for (size_t r = 0; r < layout.imageTableRangeCount; r++) {
        for (uint32_t offset = layout.imageTableRanges[r].first; offset < layout.imageTableRanges[r].last;
             offset += 12) {
            Put32(rom, table + offset, 100);
            rom[table + offset + 4] = 8;
            rom[table + offset + 5] = 8;
            rom[table + offset + 6] = layout.imageTableRanges[r].effect ? 3 : 0;
            rom[table + offset + 7] = 4;
            rom[table + offset + 9] = 2; // s: clamp
            entryCount++;
        }
    }
    std::vector<uint32_t> lists = { layout.singleImageLists[0], layout.singleImageLists[1] };
    for (uint32_t i = 0; i < layout.flareListCount; i++) {
        lists.push_back(layout.flareListFirst + i * layout.flareListStride);
    }
    for (uint32_t offset : lists) {
        Put32(rom, table + offset, kG_SETTIMG << 24);
        Put32(rom, table + offset + 4, 0xABCD0000 | 100);
        Put32(rom, table + offset + 8, kG_SETTILESIZE << 24);
        Put32(rom, table + offset + 16, kG_ENDDL << 24);
    }
    members = RunStage(PackHudImages, rom, Region::Us, error);
    Check(error.ok() && members.size() == 1 + entryCount + lists.size(), "hud member count");
    Check(entryCount == 194, "the table holds 194 entries");
    if (members.count("hud/images")) {
        const auto& m = members["hud/images"];
        std::string text(m.begin() + kPayload + 4, m.end());
        Check(text.rfind("0xac8\t8\t8\t4\n", 0) == 0, "the size table lists entries by table offset");
    }
    Check(members.count("hud/fx/0ac8") && members.count("hud/fx/1014") && members.count("hud/img/0c84") &&
              members.count("hud/img/0fd8") && !members.count("hud/img/1014") && members.count("hud/gdl/0000") &&
              members.count("hud/gdl/09a8"),
          "entries are written under the HUD's or the effects' path");
    if (members.count("hud/img/0c84") && members.count("hud/fx/0ac8")) {
        Check(Word(members["hud/img/0c84"], kPayload + 8) == (kG_SETTEX << 24 | (1 << 22) | 4),
              "a HUD request has no sample offset");
        Check(Word(members["hud/fx/0ac8"], kPayload + 8) == (kG_SETTEX << 24 | (1 << 22) | (2 << 18) | 4),
              "an effect request has the half-texel offset");
    }
    if (members.count("hud/gdl/0000")) {
        const auto& m = members["hud/gdl/0000"];
        Check(Word(m, kPayload + 8) == ((kG_SETTEX << 24) | 4) && Word(m, kPayload + 12) == 100 &&
                  m.size() == kPayload + 8 + 8 + 16 + 8,
              "a global list's image load becomes a request");
    }
}

void TestTextureHits() {
    Error error;
    auto members = RunStage(PackTextureHits, std::vector<uint8_t>(), Region::Us, error);
    bool found = members.count("textures/hits") == 1;
    Check(error.ok() && found, "hits member");
    if (found) {
        const auto& m = members["textures/hits"];
        std::string text(m.begin() + kPayload + 4, m.end());
        Check(Word(m, kPayload) == text.size(), "the blob is length-prefixed");
        Check(text.rfind("COPYICON 0 0\nX 0 0\n", 0) == 0, "hits start with the first images");
        Check(text.find("\nSTOMEMAN_L 1 1\n") != std::string::npos, "a stone image hits as stone");
        size_t lines = 0;
        for (char c : text) {
            lines += c == '\n';
        }
        Check(lines == ImageDefs().size(), "one line per image");
    }
}

void TestRegionGating() {
    std::vector<uint8_t> rom(kRomSize, 0);
    Error error;
    RunStage(PackSkyImages, rom, Region::Eur, error);
    Check(error.code == ErrorCode::RegionUnsupported, "a region without table data is refused");
}

void TestLogoAndEyeIntro() {
    const RegionConstants& constants = GetRomLayout(Region::Us).constants;
    const TextureConstants& layout = constants.textures;
    std::vector<uint8_t> rom(kRomSize, 0);

    // The logo: its signature, then three lists; the first loads one vertex from segment offset 0x100.
    const uint32_t base = constants.rarewareLogoRomStart;
    const uint8_t signature[32] = { 0xdc, 0xdc, 0xdc, 0x00, 0xdc, 0xdc, 0xdc, 0x00, 0xff, 0xff, 0xff,
                                    0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    std::copy(signature, signature + 32, rom.begin() + base);
    for (int i = 0; i < 16; i++) {
        rom[base + 0x100 + i] = uint8_t(0x10 + i);
    }
    Put32(rom, base + layout.rarewareLogoLists[0], (kG_VTX << 24) | 0x10);
    Put32(rom, base + layout.rarewareLogoLists[0] + 4, constants.rarewareLogoSegmentAddress + 0x100);
    Put32(rom, base + layout.rarewareLogoLists[0] + 8, kG_ENDDL << 24);
    for (int i = 1; i < 3; i++) {
        Put32(rom, base + layout.rarewareLogoLists[i], kG_ENDDL << 24);
    }
    Error error;
    auto members = RunStage(PackRarewareLogo, rom, Region::Us, error);
    Check(error.ok() && members.size() == 6, "logo members: a vertex array, three lists and two images");
    if (members.count("rarewarelogo/vtx/000100")) {
        const auto& m = members["rarewarelogo/vtx/000100"];
        Check(m.size() == kPayload + 4 + 16 && Word(m, kPayload) == 1 && m[kPayload + 4] == 0x10, "vertex payload");
    }
    if (members.count("rarewarelogo/dl/0043e8")) {
        const auto& m = members["rarewarelogo/dl/0043e8"];
        Check(Word(m, kPayload + 8) == ((kOtrG_VTX_HASH << 24) | (1 << 12) | (1 << 1)) && Word(m, kPayload + 12) == 0 &&
                  Word64(m, kPayload + 16) == PathHash("rarewarelogo/vtx/000100"),
              "a vertex load names its array by path hash");
    }
    if (members.count("rarewarelogo/img/004fe8")) {
        const auto& m = members["rarewarelogo/img/004fe8"];
        Check(Word(m, kPayload) == uint32_t(TextureType::RGBA16bpp) && Word(m, kPayload + 4) == 32 &&
                  Word(m, kPayload + 8) == 32 && m.size() == kPayload + 28 + 2048,
              "a code-loaded logo image is 32x32 RGBA16");
    }
    rom[base] ^= 0xFF;
    RunStage(PackRarewareLogo, rom, Region::Us, error);
    Check(error.code == ErrorCode::Corrupt, "a segment without the logo signature is refused");

    // The gun barrel: a run-length backdrop and the blood stream inside the compressed game code segment.
    const uint32_t sight = constants.eyeIntroSightRomStart;
    rom[sight] = uint8_t(layout.eyeIntroSightWidth >> 8);
    rom[sight + 1] = uint8_t(layout.eyeIntroSightWidth);
    rom[sight + 2] = uint8_t(layout.eyeIntroSightHeight >> 8);
    rom[sight + 3] = uint8_t(layout.eyeIntroSightHeight);
    const size_t pixels = size_t(layout.eyeIntroSightWidth) * layout.eyeIntroSightHeight;
    size_t at = sight + 10, produced = 0;
    for (int value = 1; produced < pixels; value++) {
        size_t run = std::min<size_t>(200, pixels - produced);
        rom[at++] = uint8_t(run);
        rom[at++] = uint8_t(value);
        produced += run;
    }
    std::vector<uint8_t> segment(layout.eyeIntroBloodOffset + layout.eyeIntroBloodSize + 64, 0);
    const uint8_t blood[12] = { 0x00, 0x20, 0x03, 0x05, 0x04, 0x02, 0x60, 0x21, 0x22, 0x01, 0xe0, 0xe0 };
    std::copy(blood, blood + 12, segment.begin() + layout.eyeIntroBloodOffset);
    segment[layout.eyeIntroBloodOffset + layout.eyeIntroBloodSize - 1] = 0x7A;
    std::vector<uint8_t> deflated;
    DeflateRaw(segment, deflated, 9);
    rom[constants.eyeIntroGameSegmentRomStart] = 0x11;
    rom[constants.eyeIntroGameSegmentRomStart + 1] = 0x72;
    std::copy(deflated.begin(), deflated.end(), rom.begin() + constants.eyeIntroGameSegmentRomStart + 2);

    members = RunStage(PackEyeIntro, rom, Region::Us, error);
    Check(error.ok() && members.size() == 2, "eye intro members");
    if (members.count("eyeintro/sight")) {
        const auto& m = members["eyeintro/sight"];
        Check(Word(m, kPayload) == uint32_t(TextureType::Grayscale8bpp) && Word(m, kPayload + 4) == 440 &&
                  Word(m, kPayload + 8) == 299 && m.size() == kPayload + 28 + pixels,
              "the backdrop is a 440x299 intensity texture");
        Check(m[kPayload + 28] == 1 && m[kPayload + 28 + 200] == 2 &&
                  m[m.size() - 1] == uint8_t((pixels - 1) / 200 + 1),
              "the backdrop's runs expand");
    }
    if (members.count("eyeintro/blood")) {
        const auto& m = members["eyeintro/blood"];
        Check(Word(m, kPayload) == layout.eyeIntroBloodSize && m[m.size() - 1] == 0x7A,
              "the blood stream is the segment's bytes");
    }
    rom[sight + 1] ^= 1;
    RunStage(PackEyeIntro, rom, Region::Us, error);
    Check(error.code == ErrorCode::Corrupt, "a backdrop of the wrong size is refused");
}

// --- real ROM ---------------------------------------------------------------------------------------------------

std::optional<fs::path> FindUsRom() {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(separator, start);
        if (end == std::string::npos) {
            end = list.size();
        }
        fs::path location = list.substr(start, end - start);
        start = end + 1;
        std::error_code ec;
        if (fs::is_regular_file(location, ec)) {
            if (IsUsRom(location)) {
                return location;
            }
        } else if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                if (entry.is_regular_file() && IsUsRom(entry.path())) {
                    return entry.path();
                }
            }
        }
    }
    return std::nullopt;
}

// Member sizes and CRCs of the Python packers' output for the US ROM (tools/o2r_packer/build_*_o2r.py).
struct Golden {
    const char* name;
    size_t size;
    uint32_t crc;
};

void TestRealRom() {
    std::optional<fs::path> rom = FindUsRom();
    if (!rom) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return;
    }
    fs::path dir = ScratchDir("rom");
    Extractor extractor;
    RegisterTexturePackers(extractor);
    Options options;
    options.romPath = *rom;
    options.outputDir = dir;
    Result result = extractor.Run(options);
    Check(result.error.ok(), "texture packers run on the US ROM");
    if (!result.error.ok()) {
        std::fprintf(stderr, "%s\n", result.error.message.c_str());
        return;
    }

    bool ok = false;
    auto textures = ReadZip(dir / "goldeneye_textures.o2r", ok);
    Check(ok && textures.size() == 6682, "the texture archive holds 2698 images, 1744 palettes and 2240 chains");
    size_t pal = 0, mip = 0;
    for (const auto& [name, bytes] : textures) {
        pal += name.size() > 4 && name.compare(name.size() - 4, 4, ".pal") == 0;
        mip += name.size() > 4 && name.compare(name.size() - 4, 4, ".mip") == 0;
    }
    Check(pal == 1744 && mip == 2240, "palette and chain counts");

    const Golden texturesGolden[] = {
        { "textures/COPYICON", 3676, 0x305cd8b3 },
        { "textures/X.pal", 604, 0xd1ce5b89 },
        { "textures/TRAIN715_5264.mip", 1492, 0xc43efd31 },
    };
    for (const Golden& g : texturesGolden) {
        auto it = textures.find(g.name);
        Check(it != textures.end() && it->second.size() == g.size && Crc32(it->second) == g.crc, g.name);
    }

    auto geometry = ReadZip(dir / "goldeneye_geometry.o2r", ok);
    Check(ok, "geometry archive is a valid zip");
    std::map<std::string, size_t> perPrefix;
    for (const auto& [name, bytes] : geometry) {
        perPrefix[name.substr(0, name.find('/'))]++;
    }
    Check(perPrefix["hud"] == 212 && perPrefix["sky"] == 3 && perPrefix["rarewarelogo"] == 10 &&
              perPrefix["eyeintro"] == 2 && perPrefix["textures"] == 1 && geometry.size() == 228,
          "geometry member counts");
    const Golden geometryGolden[] = {
        { "textures/hits", 26434, 0x25401491 },
        { "hud/images", 2866, 0xc3b6eaf2 },
        { "hud/fx/1014", 104, 0x64dfd22b },
        { "hud/gdl/0078", 192, 0xe5c7372e },
        { "sky/img/2", 104, 0xc88aa161 },
        { "eyeintro/sight", 131652, 0x094f86d5 },
        { "eyeintro/blood", 2592, 0xd4219748 },
        { "rarewarelogo/vtx/002b18", 6420, 0x614eba96 },
        { "rarewarelogo/dl/004758", 2448, 0xc16f003f },
        { "rarewarelogo/img/005ff0", 2140, 0x56763251 },
    };
    for (const Golden& g : geometryGolden) {
        auto it = geometry.find(g.name);
        Check(it != geometry.end() && it->second.size() == g.size && Crc32(it->second) == g.crc, g.name);
    }
    fs::remove_all(dir);
}

} // namespace

int main() {
    TestUncompressed();
    TestCompressed();
    TestPaletted();
    TestMip();
    TestPathHash();
    TestSkyAndHudStages();
    TestTextureHits();
    TestRegionGating();
    TestLogoAndEyeIntro();
    TestRealRom();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
