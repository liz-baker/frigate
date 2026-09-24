#include "texture/TexExpand.h"

#include <cstdio>

// Checks the G_SETTEX rewrite against command words worked out by hand from
// the RDP command layouts, using synthetic images (no ROM data).

using namespace Frigate::TexExpand;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

constexpr uint64_t kHash = 0xc0deface12345678ull;

// type 3/4, smode 0 (wrap), tmode 1 (clamp), tile 0, offset 0.
Word Request(uint32_t type) {
    return { (0xc0u << 24) | (1u << 20) | type, 0 };
}

std::vector<Word> Stream(uint32_t type, uint32_t format = 0, uint32_t size = 2, uint32_t width = 32) {
    // The packer's reference word: the image's own format, size and width.
    Word ref = { (0x20u << 24) | (format << 21) | (size << 19) | (width - 1u), 0 };
    return {
        Request(type), ref, { static_cast<uint32_t>(kHash >> 32), static_cast<uint32_t>(kHash) }, { 0xb8u << 24, 0 }
    };
}

ImageLookup Lookup(ImageInfo info) {
    return [info](uint64_t hash) -> std::optional<ImageInfo> {
        if (hash != kHash) {
            return std::nullopt;
        }
        return info;
    };
}

const ImageInfo kRgba16{ 0, 2, 32, 32 };

void TestPreswappedTile() {
    Stats stats;
    auto out = ExpandSetTex(Stream(4), Lookup(kRgba16), &stats);

    std::vector<Word> expected = {
        { 0x2010001f, 0 },          // reference kept: 16-bit, width 32
        { 0xc0deface, 0x12345678 }, // hash unchanged
        { 0xf5100000, 0x07000000 }, // load tile 7 at TMEM 0
        { 0xe6000000, 0 },          // load sync
        { 0xf3000000, 0x073ff100 }, // 1024 texels, dxt 256
        { 0xe7000000, 0 },          // pipe sync
        { 0xf5101000, 0x00094050 }, // render tile 0: clamp t, 32x32
        { 0xf2000000, 0x0007c07c }, // full-size tile extent
        { 0xb8000000, 0 },          // list end kept
    };
    Check(out == expected, "type 4 expands to one load and one tile");
    Check(stats.expanded == 1 && stats.unsupported == 0, "type 4 stats");
}

void TestEightBitLoadsAsItself() {
    auto out = ExpandSetTex(Stream(4, 4, 1), Lookup({ 4, 1, 32, 32 }));
    Check(out.size() == 9 && out[0] == Word{ 0x2088001f, 0 }, "8-bit reference kept");
    if (out.size() == 9) {
        Check(out[2] == Word{ 0xf5880000, 0x07000000 }, "8-bit load tile");
        Check(out[4] == Word{ 0xf3000000, 0x073ff200 }, "8-bit load is width x height texels");
    }
}

void TestThirtyTwoBitLoadsAsItself() {
    // 32-bit texels: the reference and load keep the size, the load is one
    // word per two texels a row, and the render tile's row is 16-bit sized.
    auto out = ExpandSetTex(Stream(4, 0, 3), Lookup({ 0, 3, 32, 32 }));
    Check(out.size() == 9 && out[0] == Word{ 0x2018001f, 0 }, "32-bit reference kept");
    if (out.size() == 9) {
        Check(out[2] == Word{ 0xf5180000, 0x07000000 }, "32-bit load tile");
        Check(out[4] == Word{ 0xf3000000, 0x073ff080 }, "32-bit load is width x height texels");
        Check(out[6] == Word{ 0xf5181000, 0x00094050 }, "32-bit render tile line");
    }
}

void TestFourBitLoadsAsBytes() {
    // 16 bytes per row; the reference's size becomes 8-bit and its width the row's bytes.
    auto out = ExpandSetTex(Stream(4, 4, 0), Lookup({ 4, 0, 32, 32 }));
    Check(out.size() == 9 && out[0] == Word{ 0x2088000f, 0 }, "4-bit reference rewritten to bytes");
    if (out.size() == 9) {
        Check(out[2] == Word{ 0xf5880000, 0x07000000 }, "4-bit load tile is 8-bit");
        Check(out[4] == Word{ 0xf3000000, 0x071ff400 }, "4-bit load is rows x row bytes");
    }

    // Rows are padded to a whole byte: 33 bytes for 65 texels.
    auto odd = ExpandSetTex(Stream(4, 4, 0, 65), Lookup({ 4, 0, 65, 65 }));
    Check(odd.size() == 9 && odd[0] == Word{ 0x20880020, 0 } && odd[4] == Word{ 0xf3000000, 0x0786019a },
          "odd 4-bit width rounds a row up to a byte");
}

void TestPalettedLoadsPaletteFirst() {
    constexpr uint64_t kPalette = 0x1122334455667788ull;
    ImageInfo ci8{ 2, 1, 32, 32, kPalette, 100 };
    auto out = ExpandSetTex(Stream(4, 2, 1), Lookup(ci8));
    Check(out.size() == 15, "paletted image adds a palette load");
    if (out.size() == 15) {
        Check(out[0] == Word{ 0x20100063, 0 } && out[1] == Word{ 0x11223344, 0x55667788 }, "palette reference");
        Check(out[2] == Word{ 0xf5000300, 0x07000000 }, "palette tile at the TMEM palette area");
        Check(out[3] == Word{ 0xe6000000, 0 } && out[4] == Word{ 0xf0000000, 0x0718c000 }, "load of 100 entries");
        Check(out[5] == Word{ 0xe7000000, 0 }, "pipe sync after the palette");
        Check(out[6] == Word{ 0x2048001f, 0 }, "image reference follows the palette");
        Check(out[8] == Word{ 0xf5480000, 0x07000000 }, "image loads with the CI format");
    }
}

void TestTileTwoTiles() {
    auto out = ExpandSetTex(Stream(3), Lookup(kRgba16));
    // Same as type 4 plus tile 1 (tile index in w1 bits 24-26, both commands).
    Check(out.size() == 11, "type 3 has a second tile setup");
    if (out.size() == 11) {
        Check(out[8] == Word{ 0xf5101000, 0x01094050 }, "tile 1 SETTILE");
        Check(out[9] == Word{ 0xf2000000, 0x0107c07c }, "tile 1 SETTILESIZE");
    }
}

void TestMipmapSetsUpBothTiles() {
    // A two-cycle combiner blends TEXEL0 with TEXEL1, so the base-level-only
    // mipmap request still needs tile 1 set up, identical to tile 0.
    auto out = ExpandSetTex(Stream(2), Lookup(kRgba16));
    Check(out.size() == 11, "type 2 has a second tile setup");
    if (out.size() == 11) {
        Check(out[8] == Word{ 0xf5101000, 0x01094050 }, "type 2 tile 1 SETTILE");
        Check(out[9] == Word{ 0xf2000000, 0x0107c07c }, "type 2 tile 1 SETTILESIZE");
    }
}

void TestOffsetBias() {
    auto in = Stream(4);
    in[0].w0 |= 2u << 18;
    auto out = ExpandSetTex(in, Lookup(kRgba16));
    Check(out.size() == 9 && out[7] == Word{ 0xf2002002, 0x0007c07c + 0x2002 },
          "offset 2 adds a half-texel to the tile extent");
}

void TestUnsupportedLeftAsStored() {
    // A paletted image and an unhandled type both pass through unchanged.
    Stats stats;
    auto paletted = Stream(4);
    Check(ExpandSetTex(paletted, Lookup({ 2, 1, 32, 32 }), &stats) == paletted && stats.unsupported == 1,
          "paletted image left as stored");

    auto lod = Stream(0);
    Check(ExpandSetTex(lod, Lookup(kRgba16), &stats) == lod && stats.unsupported == 2, "LOD type left as stored");

    auto unresolved = Stream(4);
    Check(ExpandSetTex(
              unresolved, [](uint64_t) { return std::nullopt; }, &stats) == unresolved,
          "unresolved texture left as stored");
}

void TestHashWordIsNotACommand() {
    // A hash whose high word starts with 0xc0 must not be read as a G_SETTEX.
    std::vector<Word> in = { { 0x31u << 24, 0 }, { 0xc0000000u | 4u, 0xffffffffu }, { 0xb8u << 24, 0 } };
    Stats stats;
    Check(ExpandSetTex(in, Lookup(kRgba16), &stats) == in && stats.unsupported == 0,
          "second entry of a two-entry command is skipped");
}

} // namespace

int main() {
    TestPreswappedTile();
    TestEightBitLoadsAsItself();
    TestThirtyTwoBitLoadsAsItself();
    TestFourBitLoadsAsBytes();
    TestPalettedLoadsPaletteFirst();
    TestTileTwoTiles();
    TestMipmapSetsUpBothTiles();
    TestOffsetBias();
    TestUnsupportedLeftAsStored();
    TestHashWordIsNotACommand();
    if (gFailures == 0) {
        std::printf("all TexExpand checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
