#include "TexExpand.h"

namespace Frigate::TexExpand {

namespace {

constexpr uint8_t kOpSetTex = 0xc0;
constexpr uint8_t kOpSetTimgHash = 0x20;
constexpr uint8_t kOpSetTile = 0xf5;
constexpr uint8_t kOpSetTileSize = 0xf2;
constexpr uint8_t kOpLoadBlock = 0xf3;
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
};

enum : uint32_t { kSizeBits4 = 0, kSizeBits8 = 1, kSizeBits16 = 2, kSizeBits32 = 3 };
enum : uint32_t { kFormatCi = 2 };

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

// Row stride in 64-bit TMEM words.
uint32_t LineWords(const ImageInfo& image) {
    switch (image.size) {
        case kSizeBits16:
            return (image.width + 3u) / 4u;
        case kSizeBits8:
            return (image.width + 7u) / 8u;
        default:
            return (image.width + 15u) / 16u;
    }
}

Word SetTile(uint32_t format, uint32_t size, uint32_t line, uint32_t tmem, uint32_t tile, uint32_t cmt, uint32_t maskt,
             uint32_t cms, uint32_t masks) {
    return { (uint32_t{ kOpSetTile } << 24) | (format << 21) | (size << 19) | (line << 9) | tmem,
             (tile << 24) | (cmt << 18) | (maskt << 14) | (cms << 8) | (masks << 4) };
}

Word SetTileSize(uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    return { (uint32_t{ kOpSetTileSize } << 24) | (uls << 12) | ult, (tile << 24) | (lrs << 12) | lrt };
}

Word LoadBlock(uint32_t tile, uint32_t texels, uint32_t dxt) {
    return { uint32_t{ kOpLoadBlock } << 24, (tile << 24) | ((texels - 1) << 12) | dxt };
}

// Tile 0 (and, for the two-tile type, tile 1) as the render tile, at TMEM 0.
void AppendRenderTile(std::vector<Word>& out, const ImageInfo& image, uint32_t tile, uint32_t smode, uint32_t tmode,
                      uint32_t offset) {
    uint32_t line = LineWords(image);
    // Half-texel bias applied when the request asks for a sample offset of 2
    // and the image has no explicit mip levels; only the base level is
    // decoded, so that is every image here.
    uint32_t bias = offset == 2 ? 2 : 0;
    out.push_back(SetTile(image.format, image.size, line, 0, tile, ToTileMode(tmode), DimensionToMask(image.height),
                          ToTileMode(smode), DimensionToMask(image.width)));
    out.push_back(SetTileSize(tile, bias, bias, ((image.width - 1u) << 2) + bias, ((image.height - 1u) << 2) + bias));
}

// Loads the whole image into TMEM 0 through tile 7, as 16-bit texels: the load
// count is the image size in 64-bit words times four.
void AppendLoad(std::vector<Word>& out, const Word& ref0, const Word& ref1, const ImageInfo& image) {
    uint32_t line = LineWords(image);
    uint32_t texels = image.height * line * 4u;
    uint32_t dxt = (2048u + line - 1u) / line;

    // TODO(https://github.com/liz-baker/frigate/issues/30): confirm the interpreter honors this size field.
    // Keep the reference's width field; the size field becomes the load size.
    Word timg = ref0;
    timg.w0 = (timg.w0 & ~(uint32_t{ 3 } << 19)) | (kSizeBits16 << 19);
    out.push_back(timg);
    out.push_back(ref1);
    out.push_back(SetTile(image.format, kSizeBits16, 0, 0, 7, 0, 0, 0, 0));
    out.push_back({ uint32_t{ kOpLoadSync } << 24, 0 });
    out.push_back(LoadBlock(7, texels, dxt));
    out.push_back({ uint32_t{ kOpPipeSync } << 24, 0 });
}

bool Supported(const ImageInfo& image) {
    // TODO(https://github.com/liz-baker/frigate/issues/29): 32-bit and paletted images.
    return image.size != kSizeBits32 && image.format != kFormatCi && image.width > 0 && image.height > 0;
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

            if ((type == kTypeTile || type == kTypeTilePreswapped) && image && Supported(*image)) {
                uint32_t smode = (request.w0 >> 22) & 3;
                uint32_t tmode = (request.w0 >> 20) & 3;
                uint32_t offset = (request.w0 >> 18) & 3;

                AppendLoad(out, in[i + 1], in[i + 2], *image);
                AppendRenderTile(out, *image, 0, smode, tmode, offset);
                if (type == kTypeTile) {
                    AppendRenderTile(out, *image, 1, smode, tmode, offset);
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
