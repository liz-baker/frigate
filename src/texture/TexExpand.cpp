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
uint32_t LineWords(const ImageInfo& image) {
    switch (image.size) {
        case kSizeBits32:
        case kSizeBits16:
            return (image.width + 3u) / 4u;
        case kSizeBits8:
            return (image.width + 7u) / 8u;
        default:
            return (image.width + 15u) / 16u;
    }
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
    // and the image has no explicit mip levels; only the base level is
    // decoded, so that is every image here.
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

bool Supported(const ImageInfo& image) {
    if (image.format == kFormatCi && image.paletteEntries == 0) {
        return false;
    }
    return image.width > 0 && image.height > 0;
}

} // namespace

std::vector<Word> ExpandSetTex(const std::vector<Word>& in, const ImageLookup& lookup, Stats* stats) {
    std::vector<Word> out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size();) {
        uint8_t op = in[i].w0 >> 24;
        size_t length = IsTwoEntry(op) ? 2 : 1;

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

                if (image->format == kFormatCi) {
                    AppendPaletteLoad(out, *image);
                }
                if (type == kTypeSecondTile) {
                    const uint32_t tmem = (request.w0 >> 3) & 0x1ff;
                    AppendLoad(out, in[i + 1], in[i + 2], *image, tmem);
                    const uint32_t shifts = (request.w0 >> 12) & 7;
                    const uint32_t shiftt = (request.w0 >> 15) & 7;
                    AppendRenderTile(out, *image, 1, smode, tmode, offset, tmem, shiftt, shifts);
                } else {
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
