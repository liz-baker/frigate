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

std::vector<Word> Stream(uint32_t type, uint32_t format = 0, uint32_t size = 2) {
    // Reference word: format/size are placeholders the rewrite must ignore/replace.
    Word ref = { (0x20u << 24) | (format << 21) | (size << 19) | 31u, 0 };
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
        { 0x2010001f, 0 },          // reference, load size 16-bit
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

void TestTileTwoTiles() {
    auto out = ExpandSetTex(Stream(3), Lookup(kRgba16));
    // Same as type 4 plus tile 1 (tile index in w1 bits 24-26, both commands).
    Check(out.size() == 11, "type 3 has a second tile setup");
    if (out.size() == 11) {
        Check(out[8] == Word{ 0xf5101000, 0x01094050 }, "tile 1 SETTILE");
        Check(out[9] == Word{ 0xf2000000, 0x0107c07c }, "tile 1 SETTILESIZE");
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
    TestTileTwoTiles();
    TestOffsetBias();
    TestUnsupportedLeftAsStored();
    TestHashWordIsNotACommand();
    if (gFailures == 0) {
        std::printf("all TexExpand checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
