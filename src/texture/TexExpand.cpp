#include "TexExpand.h"

namespace Frigate::TexExpand {

namespace {

constexpr uint8_t kOpSetTex = 0xc0;
constexpr uint8_t kOpSetTimgHash = 0x20;
constexpr uint8_t kOpSetTile = 0xf5;
constexpr uint8_t kOpSetTileSize = 0xf2;
constexpr uint8_t kOpLoadBlock = 0xf3;
constexpr uint8_t kOpLoadTlut = 0xf0;
constexpr uint8_t kOpLoadSync = 0xe6;
constexpr uint8_t kOpPipeSync = 0xe7;
constexpr uint8_t kOpTexture = 0xbb;
constexpr uint8_t kOpTri4 = 0xb1;
constexpr uint8_t kOpTri1 = 0xbf;

// Commands stored as two entries (command pair, then a hash pair); mirrors the
// ODLT reader's list of 128-bit commands.
bool IsTwoEntry(uint8_t op) {
    switch (op) {
        case 0x20: // G_SETTIMG_OTR_HASH
        case 0x31: // G_DL_OTR_HASH
        case 0x32: // G_VTX_OTR_HASH
        case 0x33: // G_MARKER
        case 0x35: // G_BRANCH_Z_OTR
        case 0x36: // G_MTX_OTR
        case 0x42: // G_MOVEMEM_OTR
            return true;
        default:
            return false;
    }
}

enum RequestType : uint32_t {
    kTypeLod = 0,
    kTypeDetail = 1,
    kTypeMipmap = 2,
    kTypeTile = 3,
    kTypeTilePreswapped = 4,
    // The image is loaded at the TMEM word address in bits 3 to 11 of the request
    // and sets tile 1 alone, after another image set tile 0: a surface that blends
    // two images (an explosion's flare). Bits 12 to 14 and 15 to 17 are the tile's
    // s and t coordinate shifts.
    kTypeSecondTile = 5,
};

enum : uint32_t { kSizeBits4 = 0, kSizeBits8 = 1, kSizeBits16 = 2, kSizeBits32 = 3 };
enum : uint32_t { kFormatRgba = 0, kFormatCi = 2 };

// Where a palette lives in TMEM (word address).
constexpr uint32_t kTlutTmem = 0x100;

// G_TX_* wrap modes in the tile's cm fields.
constexpr uint32_t kTxWrap = 0;
constexpr uint32_t kTxMirror = 1;
constexpr uint32_t kTxClamp = 2;

uint32_t ToTileMode(uint32_t requestMode) {
    if (requestMode == 1) {
        return kTxClamp;
    }
    if (requestMode == 2) {
        return kTxMirror;
    }
    return kTxWrap;
}

// Smallest mask (log2) covering `dimension`, capped at 8.
uint32_t DimensionToMask(uint32_t dimension) {
    uint32_t mask = 0;
    for (int32_t remaining = static_cast<int32_t>(dimension) - 1; remaining > 0 && mask < 8; remaining >>= 1) {
        mask++;
    }
    return mask;
}

// Row stride of a render tile in 64-bit TMEM words. A 32-bit texel takes two
// bytes in each of TMEM's two banks, so its row is as long as a 16-bit one.
uint32_t LineWords(uint32_t size, uint32_t width) {
    switch (size) {
        case kSizeBits32:
        case kSizeBits16:
            return (width + 3u) / 4u;
        case kSizeBits8:
            return (width + 7u) / 8u;
        default:
            return (width + 15u) / 16u;
    }
}

uint32_t LineWords(const ImageInfo& image) {
    return LineWords(image.size, image.width);
}

// Size of mip level `lod`: the game rounds each halving up.
uint32_t LevelDimension(uint32_t base, uint32_t lod) {
    for (uint32_t i = 0; i < lod; i++) {
        base = (base + 1u) >> 1;
    }
    return base;
}

// TMEM words mip level `lod` takes: its rows padded to whole words.
uint32_t LevelWords(const ImageInfo& image, uint32_t lod) {
    return LineWords(image.size, LevelDimension(image.width, lod)) * LevelDimension(image.height, lod);
}

Word SetTile(uint32_t format, uint32_t size, uint32_t line, uint32_t tmem, uint32_t tile, uint32_t cmt, uint32_t maskt,
             uint32_t cms, uint32_t masks, uint32_t shiftt = 0, uint32_t shifts = 0) {
    return { (uint32_t{ kOpSetTile } << 24) | (format << 21) | (size << 19) | (line << 9) | tmem,
             (tile << 24) | (cmt << 18) | (maskt << 14) | (shiftt << 10) | (cms << 8) | (masks << 4) | shifts };
}

Word SetTileSize(uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    return { (uint32_t{ kOpSetTileSize } << 24) | (uls << 12) | ult, (tile << 24) | (lrs << 12) | lrt };
}

Word LoadBlock(uint32_t tile, uint32_t texels, uint32_t dxt) {
    return { uint32_t{ kOpLoadBlock } << 24, (tile << 24) | ((texels - 1) << 12) | dxt };
}

// Tile 0 (and, for the two-tile type, tile 1) as the render tile, at TMEM `tmem`.
void AppendRenderTile(std::vector<Word>& out, const ImageInfo& image, uint32_t tile, uint32_t smode, uint32_t tmode,
                      uint32_t offset, uint32_t tmem = 0, uint32_t shiftt = 0, uint32_t shifts = 0) {
    uint32_t line = LineWords(image);
    // Half-texel bias applied when the request asks for a sample offset of 2
    // and the image has no explicit mip levels.
    // TODO(https://github.com/liz-baker/frigate/issues/458): the packer doesn't record explicit levels yet, so every
    // image gets the bias.
    uint32_t bias = offset == 2 ? 2 : 0;
    out.push_back(SetTile(image.format, image.size, line, tmem, tile, ToTileMode(tmode), DimensionToMask(image.height),
                          ToTileMode(smode), DimensionToMask(image.width), shiftt, shifts));
    out.push_back(SetTileSize(tile, bias, bias, ((image.width - 1u) << 2) + bias, ((image.height - 1u) << 2) + bias));
}

// Loads the whole image into TMEM at word address `tmem` through tile 7. The interpreter takes the
// row stride from the reference's width and size fields, and the resource's
// rows are not padded to whole TMEM words (a 4-bit row only to a whole byte),
// so those fields must describe the image's real rows rather than a padded
// tile line.
//
// 8- and 16-bit images load as themselves. A 4-bit image loads as bytes, one
// row being (width + 1) / 2 of them: the interpreter mishandles a 4-bit load
// size, and the load count field (12 bits) is too small for a large 4-bit
// image's texels but holds its bytes.
void AppendLoad(std::vector<Word>& out, const Word& ref0, const Word& ref1, const ImageInfo& image, uint32_t tmem = 0) {
    uint32_t line = LineWords(image);
    // The load's own row is the image's bytes in whole words.
    uint32_t loadLine = image.size == kSizeBits32 ? (image.width * 4u + 7u) / 8u : line;
    uint32_t dxt = (2048u + loadLine - 1u) / loadLine;

    Word timg = ref0;
    uint32_t loadSize = image.size;
    uint32_t rowUnits = image.width;
    if (image.size == kSizeBits4) {
        loadSize = kSizeBits8;
        rowUnits = (image.width + 1u) / 2u;
        timg.w0 = (timg.w0 & ~((uint32_t{ 3 } << 19) | 0xfffu)) | (kSizeBits8 << 19) | (rowUnits - 1u);
    }

    out.push_back(timg);
    out.push_back(ref1);
    out.push_back(SetTile(image.format, loadSize, 0, tmem, 7, 0, 0, 0, 0));
    out.push_back({ uint32_t{ kOpLoadSync } << 24, 0 });
    out.push_back(LoadBlock(7, rowUnits * image.height, dxt));
    out.push_back({ uint32_t{ kOpPipeSync } << 24, 0 });
}

// Loads a palette of RGBA5551 entries into the TMEM palette area through
// tile 7. The interpreter reads a palette load's source through the
// reference, sized as a run of 16-bit texels. The game also selects RGBA16
// lookup mode, which the interpreter does not read.
void AppendPaletteLoad(std::vector<Word>& out, const ImageInfo& image) {
    uint32_t entries = image.paletteEntries;
    out.push_back(
        { (uint32_t{ kOpSetTimgHash } << 24) | (kFormatRgba << 21) | (kSizeBits16 << 19) | (entries - 1u), 0 });
    out.push_back({ static_cast<uint32_t>(image.paletteHash >> 32), static_cast<uint32_t>(image.paletteHash) });
    out.push_back(SetTile(kFormatRgba, kSizeBits4, 1, kTlutTmem, 7, 0, 0, 0, 0));
    out.push_back({ uint32_t{ kOpLoadSync } << 24, 0 });
    out.push_back({ uint32_t{ kOpLoadTlut } << 24, (uint32_t{ 7 } << 24) | ((entries - 1u) << 14) });
    out.push_back({ uint32_t{ kOpPipeSync } << 24, 0 });
}

// Loads a `.mip` chain through tile 7 into TMEM 0, as one block like the
// game. The reference names the chain and its width field one padded base
// row, which is the stride the interpreter reads the base level with when it
// samples it alone. A 4-bit chain loads as bytes, as AppendLoad explains.
void AppendChainLoad(std::vector<Word>& out, const Word& ref0, const ImageInfo& image) {
    uint32_t rowBytes = LineWords(image) * 8u;
    uint32_t chainBytes = 0;
    for (uint32_t lod = 0; lod < image.mipLevels; lod++) {
        chainBytes += LevelWords(image, lod) * 8u;
    }

    uint32_t loadSize = image.size == kSizeBits4 ? kSizeBits8 : image.size;
    uint32_t unitBytes = loadSize == kSizeBits16 ? 2u : 1u;

    Word timg = ref0;
    timg.w0 = (timg.w0 & ~((uint32_t{ 3 } << 19) | 0xfffu)) | (loadSize << 19) | (rowBytes / unitBytes - 1u);
    out.push_back(timg);
    out.push_back({ static_cast<uint32_t>(image.mipHash >> 32), static_cast<uint32_t>(image.mipHash) });
    out.push_back(SetTile(image.format, loadSize, 0, 0, 7, 0, 0, 0, 0));
    out.push_back({ uint32_t{ kOpLoadSync } << 24, 0 });
    // The game loads a chain with no dxt: its rows are already in TMEM order.
    out.push_back(LoadBlock(7, chainBytes / unitBytes, 0));
    out.push_back({ uint32_t{ kOpPipeSync } << 24, 0 });
}

// One render tile per mip level from `baseTile`, each at the TMEM word the
// level follows the one before at, sized and masked to the level and with
// coordinate shifts of its level (texWriteTileLods in the GoldenEye decomp).
void AppendTileLods(std::vector<Word>& out, const ImageInfo& image, uint32_t baseTile, uint32_t tmem, uint32_t smode,
                    uint32_t tmode, uint32_t offset) {
    // Same half-texel bias as AppendRenderTile.
    uint32_t bias = offset == 2 ? 2 : 0;
    for (uint32_t lod = 0; lod < image.mipLevels; lod++) {
        uint32_t width = LevelDimension(image.width, lod);
        uint32_t height = LevelDimension(image.height, lod);
        uint32_t tile = baseTile + lod;
        out.push_back(SetTile(image.format, image.size, LineWords(image.size, width), tmem, tile, ToTileMode(tmode),
                              DimensionToMask(height), ToTileMode(smode), DimensionToMask(width), lod, lod));
        out.push_back(SetTileSize(tile, bias, bias, ((width - 1u) << 2) + bias, ((height - 1u) << 2) + bias));
        tmem += LevelWords(image, lod);
    }
}

// The game sets G_TEXTURE's level to the image's top mip level before each
// request (texWriteTextureCmd). The first request after a triangle command
// repeats the last G_TEXTURE with the level changed (or adds one if there is
// none yet); a later one changes the last G_TEXTURE in place.
void ApplyTextureLevel(std::vector<Word>& out, std::optional<size_t> lastTexture, bool afterTriangles, uint32_t level) {
    constexpr uint32_t kLevelMask = uint32_t{ 7 } << 11;
    if (!afterTriangles) {
        if (lastTexture) {
            out[*lastTexture].w0 = (out[*lastTexture].w0 & ~kLevelMask) | (level << 11);
        }
        return;
    }
    if (!lastTexture) {
        // gSPTexture(0xffff, 0xffff, level, G_TX_RENDERTILE, G_ON)
        out.push_back({ (uint32_t{ kOpTexture } << 24) | (level << 11) | 1u, 0xffffffffu });
        return;
    }
    Word last = out[*lastTexture];
    uint32_t w0 = (last.w0 & ~kLevelMask) | (level << 11);
    if (w0 != last.w0) {
        out.push_back({ w0, last.w1 });
    }
}

bool Supported(const ImageInfo& image) {
    if (image.format == kFormatCi && image.paletteEntries == 0) {
        return false;
    }
    return image.width > 0 && image.height > 0;
}

} // namespace

uint8_t MipLevelsForChainSize(const ImageInfo& base, uint32_t chainBytes) {
    // A 32-bit level splits across TMEM's banks, so its words aren't its bytes.
    if (base.size == kSizeBits32 || base.width == 0 || base.height == 0) {
        return 0;
    }
    uint32_t total = 0;
    // The G_TEXTURE level field holds up to 8 levels.
    for (uint32_t lod = 0; lod < 8; lod++) {
        total += LevelWords(base, lod) * 8u;
        if (total == chainBytes) {
            return static_cast<uint8_t>(lod + 1);
        }
        if (total > chainBytes) {
            break;
        }
    }
    return 0;
}

std::vector<Word> ExpandSetTex(const std::vector<Word>& in, const ImageLookup& lookup, Stats* stats) {
    std::vector<Word> out;
    out.reserve(in.size());

    // Where the last G_TEXTURE went in `out`, and whether a triangle command
    // came since the last request (see ApplyTextureLevel).
    std::optional<size_t> lastTexture;
    bool afterTriangles = true;

    for (size_t i = 0; i < in.size();) {
        uint8_t op = in[i].w0 >> 24;
        size_t length = IsTwoEntry(op) ? 2 : 1;

        if (op == kOpTexture) {
            lastTexture = out.size();
            afterTriangles = false;
        } else if (op == kOpTri4 || op == kOpTri1) {
            afterTriangles = true;
        }

        bool hasRef = op == kOpSetTex && i + 2 < in.size() && (in[i + 1].w0 >> 24) == kOpSetTimgHash;
        if (hasRef) {
            const Word& request = in[i];
            uint32_t type = request.w0 & 7;
            uint64_t hash = (uint64_t{ in[i + 2].w0 } << 32) | in[i + 2].w1;
            std::optional<ImageInfo> image = lookup(hash);

            if (type <= kTypeSecondTile && image && Supported(*image)) {
                uint32_t smode = (request.w0 >> 22) & 3;
                uint32_t tmode = (request.w0 >> 20) & 3;
                uint32_t offset = (request.w0 >> 18) & 3;
                bool chain = image->mipLevels >= 2;

                ApplyTextureLevel(out, lastTexture, afterTriangles, chain ? image->mipLevels - 1u : 0u);
                afterTriangles = false;

                if (image->format == kFormatCi) {
                    AppendPaletteLoad(out, *image);
                }
                if (type == kTypeMipmap && chain) {
                    AppendChainLoad(out, in[i + 1], *image);
                    AppendTileLods(out, *image, 0, 0, smode, tmode, offset);
                } else if (type == kTypeSecondTile) {
                    const uint32_t tmem = (request.w0 >> 3) & 0x1ff;
                    AppendLoad(out, in[i + 1], in[i + 2], *image, tmem);
                    const uint32_t shifts = (request.w0 >> 12) & 7;
                    const uint32_t shiftt = (request.w0 >> 15) & 7;
                    AppendRenderTile(out, *image, 1, smode, tmode, offset, tmem, shiftt, shifts);
                } else {
                    // TODO(https://github.com/liz-baker/frigate/issues/317): LOD and detail requests (types 0 and
                    // 1) need the detail tile 0, the prim LOD minimum and the chain from tile 1; they load the base
                    // level only for now.
                    AppendLoad(out, in[i + 1], in[i + 2], *image);
                    AppendRenderTile(out, *image, 0, smode, tmode, offset);
                    // Every type but the preswapped tile gets tile 1 too: room
                    // lists blend TEXEL0 with TEXEL1, which reads black from an
                    // unset tile.
                    if (type != kTypeTilePreswapped) {
                        AppendRenderTile(out, *image, 1, smode, tmode, offset);
                    }
                }
                if (stats) {
                    stats->expanded++;
                }
                i += 3;
                continue;
            }

            if (stats) {
                stats->unsupported++;
            }
        }

        for (size_t j = 0; j < length && i + j < in.size(); j++) {
            out.push_back(in[i + j]);
        }
        i += length;
    }

    return out;
}

} // namespace Frigate::TexExpand
