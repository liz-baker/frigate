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

// gSPTexture(0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON), as lists carry before a request.
constexpr Word kTexture{ 0xbb000001, 0xffffffff };

std::vector<Word> Stream(uint32_t type, uint32_t format = 0, uint32_t size = 2, uint32_t width = 32) {
    // The packer's reference word: the image's own format, size and width.
    Word ref = { (0x20u << 24) | (format << 21) | (size << 19) | (width - 1u), 0 };
    return { kTexture,
             Request(type),
             ref,
             { static_cast<uint32_t>(kHash >> 32), static_cast<uint32_t>(kHash) },
             { 0xb8u << 24, 0 } };
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
        kTexture,
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

void TestSecondTileLoadsAtItsAddress() {
    // Type 5: the image goes to TMEM word 0x188 and sets tile 1 only.
    auto in = Stream(5);
    in[1].w0 |= 0x188u << 3;
    in[1].w0 |= (2u << 12) | (3u << 15); // s shift 2, t shift 3
    auto out = ExpandSetTex(in, Lookup(kRgba16));

    std::vector<Word> expected = {
        kTexture,
        { 0x2010001f, 0 },
        { 0xc0deface, 0x12345678 },
        { 0xf5100188, 0x07000000 }, // load tile 7 at TMEM 0x188
        { 0xe6000000, 0 },
        { 0xf3000000, 0x073ff100 },
        { 0xe7000000, 0 },
        { 0xf5101188, 0x01094c52 }, // render tile 1 at the same address, shifts 2 and 3
        { 0xf2000000, 0x0107c07c },
        { 0xb8000000, 0 },
    };
    Check(out == expected, "type 5 loads at its TMEM address and sets tile 1");
}

void TestEightBitLoadsAsItself() {
    auto out = ExpandSetTex(Stream(4, 4, 1), Lookup({ 4, 1, 32, 32 }));
    Check(out.size() == 10 && out[1] == Word{ 0x2088001f, 0 }, "8-bit reference kept");
    if (out.size() == 10) {
        Check(out[3] == Word{ 0xf5880000, 0x07000000 }, "8-bit load tile");
        Check(out[5] == Word{ 0xf3000000, 0x073ff200 }, "8-bit load is width x height texels");
    }
}

void TestThirtyTwoBitLoadsAsItself() {
    // 32-bit texels: the reference and load keep the size, the load is one
    // word per two texels a row, and the render tile's row is 16-bit sized.
    auto out = ExpandSetTex(Stream(4, 0, 3), Lookup({ 0, 3, 32, 32 }));
    Check(out.size() == 10 && out[1] == Word{ 0x2018001f, 0 }, "32-bit reference kept");
    if (out.size() == 10) {
        Check(out[3] == Word{ 0xf5180000, 0x07000000 }, "32-bit load tile");
        Check(out[5] == Word{ 0xf3000000, 0x073ff080 }, "32-bit load is width x height texels");
        Check(out[7] == Word{ 0xf5181000, 0x00094050 }, "32-bit render tile line");
    }
}

void TestFourBitLoadsAsBytes() {
    // 16 bytes per row; the reference's size becomes 8-bit and its width the row's bytes.
    auto out = ExpandSetTex(Stream(4, 4, 0), Lookup({ 4, 0, 32, 32 }));
    Check(out.size() == 10 && out[1] == Word{ 0x2088000f, 0 }, "4-bit reference rewritten to bytes");
    if (out.size() == 10) {
        Check(out[3] == Word{ 0xf5880000, 0x07000000 }, "4-bit load tile is 8-bit");
        Check(out[5] == Word{ 0xf3000000, 0x071ff400 }, "4-bit load is rows x row bytes");
    }

    // Rows are padded to a whole byte: 33 bytes for 65 texels.
    auto odd = ExpandSetTex(Stream(4, 4, 0, 65), Lookup({ 4, 0, 65, 65 }));
    Check(odd.size() == 10 && odd[1] == Word{ 0x20880020, 0 } && odd[5] == Word{ 0xf3000000, 0x0786019a },
          "odd 4-bit width rounds a row up to a byte");
}

void TestPalettedLoadsPaletteFirst() {
    constexpr uint64_t kPalette = 0x1122334455667788ull;
    ImageInfo ci8{ 2, 1, 32, 32, kPalette, 100 };
    auto out = ExpandSetTex(Stream(4, 2, 1), Lookup(ci8));
    Check(out.size() == 16, "paletted image adds a palette load");
    if (out.size() == 16) {
        Check(out[1] == Word{ 0x20100063, 0 } && out[2] == Word{ 0x11223344, 0x55667788 }, "palette reference");
        Check(out[3] == Word{ 0xf5000300, 0x07000000 }, "palette tile at the TMEM palette area");
        Check(out[4] == Word{ 0xe6000000, 0 } && out[5] == Word{ 0xf0000000, 0x0718c000 }, "load of 100 entries");
        Check(out[6] == Word{ 0xe7000000, 0 }, "pipe sync after the palette");
        Check(out[7] == Word{ 0x2048001f, 0 }, "image reference follows the palette");
        Check(out[9] == Word{ 0xf5480000, 0x07000000 }, "image loads with the CI format");
    }
}

void TestTileTwoTiles() {
    auto out = ExpandSetTex(Stream(3), Lookup(kRgba16));
    // Same as type 4 plus tile 1 (tile index in w1 bits 24-26, both commands).
    Check(out.size() == 12, "type 3 has a second tile setup");
    if (out.size() == 12) {
        Check(out[9] == Word{ 0xf5101000, 0x01094050 }, "tile 1 SETTILE");
        Check(out[10] == Word{ 0xf2000000, 0x0107c07c }, "tile 1 SETTILESIZE");
    }
}

void TestMipmapSetsUpBothTiles() {
    // A two-cycle combiner blends TEXEL0 with TEXEL1, so the base-level-only
    // mipmap request still needs tile 1 set up, identical to tile 0.
    auto out = ExpandSetTex(Stream(2), Lookup(kRgba16));
    Check(out.size() == 12, "type 2 has a second tile setup");
    if (out.size() == 12) {
        Check(out[9] == Word{ 0xf5101000, 0x01094050 }, "type 2 tile 1 SETTILE");
        Check(out[10] == Word{ 0xf2000000, 0x0107c07c }, "type 2 tile 1 SETTILESIZE");
    }
}

void TestLodAndDetailExpandAsBaseLevel() {
    // Neither type has its extra level (lower mips, detail image) to load, so
    // both expand as the base level on tiles 0 and 1, like a mipmap request.
    auto mipmap = ExpandSetTex(Stream(2), Lookup(kRgba16));
    Check(ExpandSetTex(Stream(0), Lookup(kRgba16)) == mipmap, "LOD type expands like a mipmap request");
    Check(ExpandSetTex(Stream(1), Lookup(kRgba16)) == mipmap, "detail type expands like a mipmap request");
}

void TestOffsetBias() {
    auto in = Stream(4);
    in[1].w0 |= 2u << 18;
    auto out = ExpandSetTex(in, Lookup(kRgba16));
    Check(out.size() == 10 && out[8] == Word{ 0xf2002002, 0x0007c07c + 0x2002 },
          "offset 2 adds a half-texel to the tile extent");
}

void TestUnsupportedLeftAsStored() {
    // A paletted image and an unhandled type both pass through unchanged.
    Stats stats;
    auto paletted = Stream(4);
    Check(ExpandSetTex(paletted, Lookup({ 2, 1, 32, 32 }), &stats) == paletted && stats.unsupported == 1,
          "paletted image left as stored");

    auto unknown = Stream(6);
    Check(ExpandSetTex(unknown, Lookup(kRgba16), &stats) == unknown && stats.unsupported == 2,
          "unknown type left as stored");

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

constexpr uint64_t kMipHash = 0x0123456789abcdefull;

// 8x8 RGBA16 with a four-level chain: 128 + 32 + 16 + 8 bytes.
ImageInfo MipmappedRgba16() {
    ImageInfo info{ 0, 2, 8, 8 };
    info.mipHash = kMipHash;
    info.mipLevels = 4;
    return info;
}

void TestMipLevelsForChainSize() {
    ImageInfo rgba16{ 0, 2, 8, 8 };
    Check(MipLevelsForChainSize(rgba16, 184) == 4, "four levels of an 8x8 RGBA16 chain");
    Check(MipLevelsForChainSize(rgba16, 128) == 1, "base level alone");
    Check(MipLevelsForChainSize(rgba16, 180) == 0, "a size no level count adds up to");
    // Halvings round up: 5x3 -> 3x2 -> 2x1, rows padded to 8 bytes for 8-bit.
    Check(MipLevelsForChainSize({ 0, 1, 5, 3 }, 24 + 16 + 8) == 3, "odd sizes round up");
    Check(MipLevelsForChainSize({ 0, 3, 4, 4 }, 64) == 0, "no chains for 32-bit images");
}

void TestMipmapLoadsTheChainAndSetsATilePerLevel() {
    Stats stats;
    auto out = ExpandSetTex(Stream(2), Lookup(MipmappedRgba16()), &stats);

    std::vector<Word> expected = {
        { 0xbb001801, 0xffffffff }, // G_TEXTURE level set to the top level, 3
        { 0x20100007, 0 },          // chain reference: 16-bit, one padded base row of 8 texels
        { 0x01234567, 0x89abcdef }, // the .mip hash
        { 0xf5100000, 0x07000000 }, // load tile 7 at TMEM 0
        { 0xe6000000, 0 },          //
        { 0xf3000000, 0x0705b000 }, // 92 texels (184 bytes), no dxt
        { 0xe7000000, 0 },          //
        { 0xf5100400, 0x0008c030 }, // level 0: tile 0, line 2, TMEM 0, clamp t, masks 3
        { 0xf2000000, 0x0001c01c }, // 8x8
        { 0xf5100210, 0x01088421 }, // level 1: tile 1, line 1, TMEM 16, masks 2, shifts 1
        { 0xf2000000, 0x0100c00c }, // 4x4
        { 0xf5100214, 0x02084812 }, // level 2: tile 2, TMEM 20, masks 1, shifts 2
        { 0xf2000000, 0x02004004 }, // 2x2
        { 0xf5100216, 0x03080c03 }, // level 3: tile 3, TMEM 22, masks 0, shifts 3
        { 0xf2000000, 0x03000000 }, // 1x1
        { 0xb8000000, 0 },
    };
    Check(out == expected, "mipmap request loads the chain and sets one tile per level");
    Check(stats.expanded == 1, "chain request counted as expanded");
}

void TestFourBitChainLoadsAsBytes() {
    // I4 32x32, two levels: 16-byte rows (512) and an 8-byte row for 16x16 (128).
    ImageInfo i4{ 4, 0, 32, 32 };
    i4.mipHash = kMipHash;
    i4.mipLevels = 2;
    auto out = ExpandSetTex(Stream(2, 4, 0), Lookup(i4));
    Check(out.size() >= 7, "4-bit chain expands");
    if (out.size() >= 7) {
        Check(out[1] == Word{ 0x2088000f, 0 }, "4-bit chain reference is 16 bytes a row");
        Check(out[3] == Word{ 0xf5880000, 0x07000000 }, "4-bit chain load tile is 8-bit");
        Check(out[5] == Word{ 0xf3000000, 0x0727f000 }, "4-bit chain loads its 640 bytes");
    }
}

void TestPalettedChainLoadsPaletteFirst() {
    ImageInfo ci8{ 2, 1, 8, 8, 0x1122334455667788ull, 16 };
    ci8.mipHash = kMipHash;
    ci8.mipLevels = 2;
    auto out = ExpandSetTex(Stream(2, 2, 1, 8), Lookup(ci8));
    Check(out.size() >= 9, "paletted chain expands");
    if (out.size() >= 9) {
        Check(out[2] == Word{ 0x11223344, 0x55667788 }, "palette first");
        Check(out[7] == Word{ 0x20480007, 0 }, "then the chain reference");
        Check(out[8] == Word{ 0x01234567, 0x89abcdef }, "chain hash");
    }
}

void TestChainOnlyForMipmapRequests() {
    // A tile request of a mipmapped image still loads its base level alone.
    auto chained = ExpandSetTex(Stream(3), Lookup(MipmappedRgba16()));
    auto plain = ExpandSetTex(Stream(3), Lookup({ 0, 2, 8, 8 }));
    Check(chained.size() == plain.size() && chained[2] == plain[2], "tile request ignores the chain");
    // G_TEXTURE still gets the image's top level.
    Check(chained[0] == Word{ 0xbb001801, 0xffffffff }, "tile request still sets the G_TEXTURE level");
}

void TestTextureLevelWithoutAPrecedingTexture() {
    // No G_TEXTURE yet: the game adds gSPTexture(0xffff, 0xffff, level, 0, G_ON).
    auto in = Stream(2);
    in.erase(in.begin());
    auto out = ExpandSetTex(in, Lookup(MipmappedRgba16()));
    Check(!out.empty() && out[0] == Word{ 0xbb001801, 0xffffffff }, "G_TEXTURE added before the first request");
}

void TestTextureLevelAfterTriangles() {
    // After a triangle command, a changed level is a new copy of the last
    // G_TEXTURE; the one the earlier triangles used stays as it was.
    auto first = Stream(2);
    first.pop_back();
    std::vector<Word> in = first;
    in.push_back({ 0xbfu << 24, 0 }); // G_TRI1
    in.push_back(Request(2));
    in.push_back(first[2]);
    in.push_back(first[3]);
    in.push_back({ 0xb8u << 24, 0 });

    auto out = ExpandSetTex(in, Lookup(MipmappedRgba16()));
    Check(out[0] == Word{ 0xbb001801, 0xffffffff }, "first request sets the level in place");

    size_t triangle = 0;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i].w0 == 0xbf000000u) {
            triangle = i;
        }
    }
    Check(triangle > 0 && triangle + 1 < out.size(), "triangle kept");
    // Level already 3: nothing to repeat, the request's load follows directly.
    Check(out[triangle + 1] == Word{ 0x20100007, 0 }, "no repeat when the level is unchanged");

    // A base-level image next: the copy goes back to level 0.
    auto mixed = in;
    int calls = 0;
    auto lookup = [&calls](uint64_t) -> std::optional<ImageInfo> {
        return calls++ == 0 ? MipmappedRgba16() : ImageInfo{ 0, 2, 8, 8 };
    };
    auto out2 = ExpandSetTex(mixed, lookup);
    size_t tri2 = 0;
    for (size_t i = 0; i < out2.size(); i++) {
        if (out2[i].w0 == 0xbf000000u) {
            tri2 = i;
        }
    }
    Check(tri2 > 0 && out2[tri2 + 1] == Word{ 0xbb000001, 0xffffffff }, "a changed level repeats G_TEXTURE");
}

} // namespace

int main() {
    TestPreswappedTile();
    TestSecondTileLoadsAtItsAddress();
    TestEightBitLoadsAsItself();
    TestThirtyTwoBitLoadsAsItself();
    TestFourBitLoadsAsBytes();
    TestPalettedLoadsPaletteFirst();
    TestTileTwoTiles();
    TestMipmapSetsUpBothTiles();
    TestLodAndDetailExpandAsBaseLevel();
    TestOffsetBias();
    TestUnsupportedLeftAsStored();
    TestHashWordIsNotACommand();
    TestMipLevelsForChainSize();
    TestMipmapLoadsTheChainAndSetsATilePerLevel();
    TestFourBitChainLoadsAsBytes();
    TestPalettedChainLoadsPaletteFirst();
    TestChainOnlyForMipmapRequests();
    TestTextureLevelWithoutAPrecedingTexture();
    TestTextureLevelAfterTriangles();
    if (gFailures == 0) {
        std::printf("all TexExpand checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
